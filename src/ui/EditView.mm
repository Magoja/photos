// EditView.mm — non-destructive edit overlay (Adjust + Crop modes)
#import <Metal/Metal.h>

#include "EditView.h"
#include "command/CommandRegistry.h"
#include "util/PixelPipeline.h"
#include "import/RawDecoder.h"
#include "util/Platform.h"

#include <libraw/libraw.h>
#include <turbojpeg.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace {
using namespace std::string_view_literals;

enum class AspectConstraint { Free, Original, Fixed };

struct AspectRatio {
  std::string_view label;
  AspectConstraint constraint;
  float            ratio = 1.f;  // only used when constraint == Fixed
};
constexpr std::array<AspectRatio, 5> kAspectRatios = {{
  {"Free"sv,     AspectConstraint::Free},
  {"Original"sv, AspectConstraint::Original},
  {"1:1"sv,      AspectConstraint::Fixed, 1.f},
  {"2:3"sv,      AspectConstraint::Fixed, 2.f/3.f},
  {"3:2"sv,      AspectConstraint::Fixed, 3.f/2.f},
}};


static std::array<ImVec2, 8> cropHandlePositions(float cx, float cy, float cw, float ch) {
  return {{
    {cx,      cy},        // TL
    {cx+cw/2, cy},        // T
    {cx+cw,   cy},        // TR
    {cx,      cy+ch/2},   // L
    {cx+cw,   cy+ch/2},   // R
    {cx,      cy+ch},     // BL
    {cx+cw/2, cy+ch},     // B
    {cx+cw,   cy+ch},     // BR
  }};
}

static id<MTLTexture> rgbaToTexture(id<MTLDevice> dev,
                                    const std::vector<uint8_t>& rgba, int w, int h) {
  MTLTextureDescriptor* desc =
    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                       width:w
                                                      height:h
                                                   mipmapped:NO];
  desc.storageMode = MTLStorageModeShared;
  desc.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> tex = [dev newTextureWithDescriptor:desc];
  [tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
         mipmapLevel:0
           withBytes:rgba.data()
         bytesPerRow:w * 4];
  return tex;
}

}  // namespace

namespace ui {

// ── Constructor / Destructor ──────────────────────────────────────────────────

EditView::EditView(catalog::PhotoRepository& repo,
                   catalog::ThumbnailCache&  thumbCache,
                   TextureManager&           texMgr,
                   MTLDevicePtr              device)
  : repo_(repo), thumbCache_(thumbCache), texMgr_(texMgr), device_(device) {}

EditView::~EditView() {
  fullDecodeCancel_ = true;
  if (loadThread_.joinable()) { loadThread_.join(); }
  releasePreviewTex();
  [fallbackTex_ release];
  fallbackTex_ = nullptr;
  if (saveThread_.joinable()) { saveThread_.join(); }
}

// ── open / close ─────────────────────────────────────────────────────────────

void EditView::open(int64_t photoId) {
  // Cancel + join any in-flight load from a previous photo
  fullDecodeCancel_ = true;
  if (loadThread_.joinable()) { loadThread_.join(); }
  fullDecodeCancel_ = false;
  fullDecodeReady_  = false;
  pendingRgb_.clear();

  photoId_ = photoId;
  open_          = true;
  justOpened_    = true;
  mode_          = EditMode::Adjust;
  tabSyncNeeded_ = false;
  dragHandle_ = -1;
  aspectMode_ = 0;

  // Clear source pixels and release both Metal textures from the previous photo
  // so drawPreview shows nothing until the new decode completes.
  originalRgb_.clear();
  srcW_ = 0;
  srcH_ = 0;
  releasePreviewTex();
  [fallbackTex_ release];
  fallbackTex_ = nullptr;

  // Load existing edit settings from DB
  const auto rec = repo_.findById(photoId);
  if (rec) {
    settings_ = catalog::EditSettings::fromJson(rec->editSettings);
    origW_ = rec->widthPx;
    origH_ = rec->heightPx;
    thumbIsPreCropped_ = rec->thumbPath.find("thumbs_edit") != std::string::npos;
  } else {
    settings_ = {};
    origW_ = 0;
    origH_ = 0;
    thumbIsPreCropped_ = false;
  }
  saved_ = settings_;

  // Start accurate LibRaw decode in background immediately
  std::string srcPath;
  if (rec) {
    srcPath = repo_.fullPathFor(rec->folderId, rec->filename);
  }
  if (!srcPath.empty()) {
    fullDecoding_ = true;
    loadThread_ = std::thread([this, srcPath = std::move(srcPath)]() {
      loadLibRawBackground(srcPath);
    });
  }
  previewDirty_ = true;
}

void EditView::close() {
  open_ = false;
}

void EditView::setMode(EditMode mode) {
  if (mode_ == mode) { return; }
  mode_          = mode;
  tabSyncNeeded_ = true;
  previewDirty_  = true;
}

// ── Source pixel loading ──────────────────────────────────────────────────────

// Runs on loadThread_. Full LibRaw decode with identical params to Exporter::exportOne
// so the preview and the exported JPEG start from the same pixel values.
// Writes to pendingRgb_/pendingW_/pendingH_, then sets fullDecodeReady_ = true.
void EditView::loadLibRawBackground(std::string srcPath) {
  auto raw = std::make_unique<LibRaw>();
  raw->imgdata.params.output_bps    = 8;
  raw->imgdata.params.use_camera_wb = 1;
  if (raw->open_file(srcPath.c_str()) != LIBRAW_SUCCESS ||
      raw->unpack()                    != LIBRAW_SUCCESS ||
      raw->dcraw_process()             != LIBRAW_SUCCESS) {
    spdlog::warn("EditView: LibRaw decode failed for {}", srcPath);
    fullDecodeReady_ = true;
    return;
  }
  if (fullDecodeCancel_.load()) { return; }

  libraw_processed_image_t* img = raw->dcraw_make_mem_image();
  if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3) {
    if (img) { LibRaw::dcraw_clear_mem(img); }
    spdlog::warn("EditView: unexpected LibRaw image format for {}", srcPath);
    fullDecodeReady_ = true;
    return;
  }

  // Downsample to ≤2000px on the long edge for fast interactive editing.
  // Box-filter averaging is linear and preserves tonal response.
  constexpr int kMaxEdge = 2000;
  const int scale = std::max(1, std::max(img->width, img->height) / kMaxEdge);
  const auto dsRgb = util::downsampleRgb(img->data, img->width, img->height, scale,
                                         pendingW_, pendingH_);
  LibRaw::dcraw_clear_mem(img);

  pendingRgb_ = dsRgb;

  if (!fullDecodeCancel_.load()) {
    fullDecodeReady_ = true;
  }
}

// Called from render() on the main thread. Swaps accurate pixels when background decode is done.
void EditView::pollLibRawLoad() {
  if (!fullDecoding_ || !fullDecodeReady_.load()) { return; }
  loadThread_.join();
  originalRgb_ = std::move(pendingRgb_);
  srcW_ = pendingW_;
  srcH_ = pendingH_;
  fullDecoding_    = false;
  fullDecodeReady_ = false;
  previewDirty_    = true;  // rebuild preview with accurate base
}

// ── Pixel editing pipeline ────────────────────────────────────────────────────

// Delegated to util::applyAdjustments (shared with Exporter pipeline).
std::vector<uint8_t> EditView::applyEditsToPixels(
    const std::vector<uint8_t>& src, int w, int h,
    const catalog::EditSettings& s) const {
  return util::applyAdjustments(src, w, h, s);
}

// ── Crop/rotate helpers ───────────────────────────────────────────────────────

// ── Metal texture management ──────────────────────────────────────────────────

void EditView::releasePreviewTex() {
  if (previewTex_) {
    [previewTex_ release];
    previewTex_ = nil;
  }
}

void EditView::rebuildPreviewTexture() {
  if (originalRgb_.empty() || srcW_ <= 0 || srcH_ <= 0) {
    return;
  }
  const auto edited = applyEditsToPixels(originalRgb_, srcW_, srcH_, settings_);

  // Crop mode shows the full image so overlay handles are meaningful, but applies
  // rotation so the straighten slider gives live feedback.
  // Adjust mode shows only the cropped+rotated region so sliders reflect the final result.
  int previewW = srcW_, previewH = srcH_;
  const std::vector<uint8_t>* pixels = &edited;
  std::vector<uint8_t> croppedBuf;
  if (mode_ == EditMode::Adjust) {
    croppedBuf = util::cropAndRotatePixels(edited, srcW_, srcH_, settings_.crop,
                                          previewW, previewH);
    pixels = &croppedBuf;
  }

  releasePreviewTex();

  const auto rgba = util::rgbToRgba(*pixels, previewW * previewH);
  previewTex_ = rgbaToTexture((id<MTLDevice>)device_, rgba, previewW, previewH);
}

// ── Crop constraint ───────────────────────────────────────────────────────────

void EditView::applyCropConstraint(int handle) {
  const AspectConstraint c = kAspectRatios[aspectMode_].constraint;
  if (c == AspectConstraint::Free) {
    return;
  }
  // Desired aspect ratio in PIXEL space (width / height).
  const float physicalTarget = (c == AspectConstraint::Original)
    ? ((origW_ > 0 && origH_ > 0)
         ? static_cast<float>(origW_) / static_cast<float>(origH_)
         : static_cast<float>(srcW_)  / static_cast<float>(srcH_))  // fallback
    : kAspectRatios[aspectMode_].ratio;

  // Convert to NORMALIZED space: ncw/nch = physicalTarget / srcAspect.
  // The crop coords are fractions of srcW_/srcH_, so physical = normalized * srcAspect.
  const float srcAspect = (srcW_ > 0 && srcH_ > 0)
    ? static_cast<float>(srcW_) / static_cast<float>(srcH_)
    : 1.f;
  const float target = physicalTarget / srcAspect;

  float& ncx = settings_.crop.x;
  float& ncy = settings_.crop.y;
  float& ncw = settings_.crop.w;
  float& nch = settings_.crop.h;

  if (handle == -1) {
    // Mode-switch: fit the constrained box inside the current crop without overflow.
    const float candidate_h = ncw / target;
    if (candidate_h <= nch) {
      nch = candidate_h;      // shrink height to match width
    } else {
      ncw = nch * target;     // shrink width to match height
    }
  } else {
    // Drag: derive from the axis determined by handle.
    const bool deriveHeightFromWidth = (handle != 3 && handle != 4 && handle != 5);
    if (deriveHeightFromWidth) { nch = ncw / target; }
    else                        { ncw = nch * target; }
  }

  // Fix position so the anchor corner stays put (skip when called from mode-switch, handle == -1).
  if (handle >= 0) {
    const float anchorRight  = dragOrigX_ + dragOrigW_;
    const float anchorBottom = dragOrigY_ + dragOrigH_;
    if (handle == 0 || handle == 3 || handle == 5) { ncx = anchorRight  - ncw; }
    if (handle == 0 || handle == 1 || handle == 2) { ncy = anchorBottom - nch; }
  }

  ncx = std::clamp(ncx, 0.f, 0.99f);
  ncy = std::clamp(ncy, 0.f, 0.99f);
  ncw = std::clamp(ncw, 0.01f, 1.f - ncx);
  nch = ncw / target;                          // re-derive after width clamp
  nch = std::clamp(nch, 0.01f, 1.f - ncy);
  // If height hit minimum, re-derive width from the (now clamped) height.
  if (nch <= 0.01f + 1e-4f) {
    ncw = nch * target;
    ncw = std::clamp(ncw, 0.01f, 1.f - ncx);
  }
}

// ── Crop overlay rendering ────────────────────────────────────────────────────

void EditView::renderCropOverlay(ImDrawList* dl, ImVec2 imgMin, ImVec2 imgMax) const {
  const float imgW = imgMax.x - imgMin.x;
  const float imgH = imgMax.y - imgMin.y;
  const float cx = imgMin.x + settings_.crop.x * imgW;
  const float cy = imgMin.y + settings_.crop.y * imgH;
  const float cw = settings_.crop.w * imgW;
  const float ch = settings_.crop.h * imgH;

  const ImU32 darkCol = IM_COL32(0, 0, 0, 160);

  // Four darkened regions outside the crop
  dl->AddRectFilled(imgMin,          {imgMax.x, cy},     darkCol);  // top
  dl->AddRectFilled({imgMin.x, cy+ch}, imgMax,           darkCol);  // bottom
  dl->AddRectFilled({imgMin.x, cy},  {cx, cy+ch},        darkCol);  // left
  dl->AddRectFilled({cx+cw, cy},     {imgMax.x, cy+ch},  darkCol);  // right

  // White crop border
  dl->AddRect({cx, cy}, {cx+cw, cy+ch}, IM_COL32_WHITE, 0.f, 0, 1.5f);

  const auto handles = cropHandlePositions(cx, cy, cw, ch);
  for (const auto& h : handles) {
    dl->AddRectFilled({h.x-6.f, h.y-6.f}, {h.x+6.f, h.y+6.f}, IM_COL32_WHITE);
  }

  // Horizontal center guideline — visible while the straighten slider is dragged
  if (straightenDragging_) {
    const float midY = cy + ch * 0.5f;
    dl->AddLine({cx, midY}, {cx + cw, midY}, IM_COL32(255, 255, 100, 200), 1.f);
  }
}

// ── Crop drag handling ────────────────────────────────────────────────────────

void EditView::handleCropDrag(ImVec2 imgMin, ImVec2 imgMax) {
  const float imgW = imgMax.x - imgMin.x;
  const float imgH = imgMax.y - imgMin.y;
  const float cx = imgMin.x + settings_.crop.x * imgW;
  const float cy = imgMin.y + settings_.crop.y * imgH;
  const float cw = settings_.crop.w * imgW;
  const float ch = settings_.crop.h * imgH;

  const auto handles = cropHandlePositions(cx, cy, cw, ch);

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ImVec2 mp = ImGui::GetMousePos();
    dragHandle_ = -1;
    for (int i = 0; i < 8; ++i) {
      if (std::abs(mp.x - handles[i].x) < 10.f &&
          std::abs(mp.y - handles[i].y) < 10.f) {
        dragHandle_ = i;
        dragStart_  = mp;
        dragOrigX_  = settings_.crop.x;
        dragOrigY_  = settings_.crop.y;
        dragOrigW_  = settings_.crop.w;
        dragOrigH_  = settings_.crop.h;
        break;
      }
    }
    if (dragHandle_ == -1) {
      const bool insideX = mp.x >= cx && mp.x <= cx + cw;
      const bool insideY = mp.y >= cy && mp.y <= cy + ch;
      if (insideX && insideY) {
        dragHandle_ = 8;
        dragStart_  = mp;
        dragOrigX_  = settings_.crop.x;
        dragOrigY_  = settings_.crop.y;
        dragOrigW_  = settings_.crop.w;
        dragOrigH_  = settings_.crop.h;
      }
    }
  }

  if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && dragHandle_ >= 0) {
    const ImVec2 mp = ImGui::GetMousePos();
    const float dx = (mp.x - dragStart_.x) / imgW;
    const float dy = (mp.y - dragStart_.y) / imgH;

    float& ncx = settings_.crop.x;
    float& ncy = settings_.crop.y;
    float& ncw = settings_.crop.w;
    float& nch = settings_.crop.h;
    ncx = dragOrigX_;
    ncy = dragOrigY_;
    ncw = dragOrigW_;
    nch = dragOrigH_;

    switch (dragHandle_) {
      case 0: ncx += dx; ncy += dy; ncw -= dx; nch -= dy; break;  // TL
      case 1: ncy += dy; nch -= dy; break;                         // T
      case 2: ncy += dy; ncw += dx; nch -= dy; break;              // TR
      case 3: ncx += dx; ncw -= dx; break;                         // L
      case 4: ncw += dx; break;                                     // R
      case 5: ncx += dx; ncw -= dx; nch += dy; break;              // BL
      case 6: nch += dy; break;                                     // B
      case 7: ncw += dx; nch += dy; break;                         // BR
      case 8:                                                       // Move
        ncx = std::clamp(dragOrigX_ + dx, 0.f, 1.f - dragOrigW_);
        ncy = std::clamp(dragOrigY_ + dy, 0.f, 1.f - dragOrigH_);
        break;
    }
    ncx = std::clamp(ncx, 0.f, 0.99f);
    ncy = std::clamp(ncy, 0.f, 0.99f);
    ncw = std::clamp(ncw, 0.01f, 1.f - ncx);
    nch = std::clamp(nch, 0.01f, 1.f - ncy);
    applyCropConstraint(dragHandle_);
    previewDirty_ = true;
  }

  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    dragHandle_ = -1;
  }
}

// ── Panel renderers ───────────────────────────────────────────────────────────

bool EditView::renderSliderRow(const char* label, float* v,
                               float vmin, float vmax, float step) {
  bool changed = false;
  const std::string slId = std::string("##sl_") + label;
  const std::string inId = std::string("##in_") + label;
  ImGui::PushItemWidth(160.f);
  if (ImGui::SliderFloat(slId.c_str(), v, vmin, vmax)) {
    changed = true;
  }
  ImGui::PopItemWidth();
  ImGui::SameLine();
  ImGui::PushItemWidth(70.f);
  if (ImGui::InputFloat(inId.c_str(), v, step, step * 10.f, "%.2f")) {
    *v = std::clamp(*v, vmin, vmax);
    changed = true;
  }
  ImGui::PopItemWidth();
  ImGui::SameLine();
  ImGui::Text("%s", label);
  if (changed) {
    previewDirty_ = true;
  }
  return changed;
}

void EditView::renderAdjustPanel() {
  renderSliderRow("Exposure",    &settings_.exposure,    -3.f,  3.f,   0.05f);
  renderSliderRow("Temperature", &settings_.temperature, -100.f, 100.f, 1.f);
  renderSliderRow("Contrast",    &settings_.contrast,    -100.f, 100.f, 1.f);
  renderSliderRow("Saturation",  &settings_.saturation,  -100.f, 100.f, 1.f);
}

void EditView::renderCropPanel() {
  const float itemH = ImGui::GetTextLineHeightWithSpacing();
  const float listH = itemH * static_cast<float>(kAspectRatios.size()) + 4.f;
  ImGui::BeginChild("##aspect_list", ImVec2(120.f, listH), true);
  for (int i = 0; i < static_cast<int>(kAspectRatios.size()); ++i) {
    if (ImGui::Selectable(kAspectRatios[i].label.data(), aspectMode_ == i)) {
      aspectMode_ = i;
      applyCropConstraint(-1);
      previewDirty_ = true;
    }
  }
  ImGui::EndChild();
}

void EditView::renderStraightenBar(float previewW, float screenH) {
  constexpr float kBarH = 64.f;
  ImGui::SetNextWindowPos({0.f, screenH - kBarH});
  ImGui::SetNextWindowSize({previewW, kBarH});
  ImGui::SetNextWindowBgAlpha(0.75f);
  ImGui::Begin("##straighten_bar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
  ImGui::Text("Straighten");
  ImGui::SameLine(0.f, 12.f);
  ImGui::PushItemWidth(previewW - 160.f);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 18.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 8.f));
  ImGui::SliderFloat("##straighten", &settings_.crop.angleDeg, -45.f, 45.f, "%.1f°");
  straightenDragging_ = ImGui::IsItemActive();
  ImGui::PopStyleVar(2);
  ImGui::PopItemWidth();
  ImGui::SameLine(0.f, 12.f);
  if (ImGui::SmallButton("Reset")) {
    settings_.crop.angleDeg = 0.f;
  }

  ImGui::End();
}

// ── Preview drawing ───────────────────────────────────────────────────────────

void EditView::drawPreview(ImDrawList* dl, ImVec2 areaMin, ImVec2 areaMax) {
  if (previewDirty_ && !originalRgb_.empty()) {
    rebuildPreviewTexture();
    previewDirty_ = false;
  }

  if (!previewTex_) {
    // LibRaw decode still in progress; renderPreviewArea already draws the
    // dark background + "Loading accurate preview..." overlay.
    // Release any previously retained fallback.
    if (fallbackTex_) { [fallbackTex_ release]; fallbackTex_ = nullptr; }
    return;
  }
  MTLTexturePtr displayTex = previewTex_;

  const float areaW = (areaMax.x - areaMin.x) * 0.9f;
  const float areaH = (areaMax.y - areaMin.y) * 0.9f;
  // When using the fallback thumbnail in Adjust mode, derive aspect ratio from
  // the crop region so the letterboxed placeholder matches the LibRaw preview.
  // Skip crop scaling if the thumbnail already incorporates the crop (thumbs_edit).
  const float imgAspect = [&]() -> float {
    if (previewTex_ != nullptr) {
      return (float)displayTex.width / (float)displayTex.height;
    }
    if (mode_ == EditMode::Adjust) {
      if (thumbIsPreCropped_) {
        return (float)displayTex.width / (float)displayTex.height;
      }
      return (settings_.crop.w * (float)displayTex.width) /
             (settings_.crop.h * (float)displayTex.height);
    }
    return (float)displayTex.width / (float)displayTex.height;
  }();
  const float areaAspect = areaW / areaH;

  float imgW, imgH;
  if (imgAspect > areaAspect) {
    imgW = areaW;
    imgH = areaW / imgAspect;
  } else {
    imgH = areaH;
    imgW = areaH * imgAspect;
  }
  const ImVec2 imgMin = {
    areaMin.x + (areaMax.x - areaMin.x - imgW) * 0.5f,
    areaMin.y + (areaMax.y - areaMin.y - imgH) * 0.5f,
  };
  const ImVec2 imgMax = {imgMin.x + imgW, imgMin.y + imgH};

  if (mode_ == EditMode::Crop && settings_.crop.angleDeg != 0.f) {
    const float angleRad = settings_.crop.angleDeg * (std::numbers::pi_v<float> / 180.f);
    const float cosA = cosf(angleRad), sinA = sinf(angleRad);
    const float imgW = imgMax.x - imgMin.x;
    const float imgH = imgMax.y - imgMin.y;
    const ImVec2 center = {
      imgMin.x + (settings_.crop.x + settings_.crop.w * 0.5f) * imgW,
      imgMin.y + (settings_.crop.y + settings_.crop.h * 0.5f) * imgH,
    };
    const auto rotPt = [&](float px, float py) -> ImVec2 {
      const float dx = px - center.x, dy = py - center.y;
      return {center.x + dx * cosA - dy * sinA,
              center.y + dx * sinA + dy * cosA};
    };
    dl->AddImageQuad(reinterpret_cast<ImTextureID>(displayTex),
                     rotPt(imgMin.x, imgMin.y), rotPt(imgMax.x, imgMin.y),
                     rotPt(imgMax.x, imgMax.y), rotPt(imgMin.x, imgMax.y),
                     {0, 0}, {1, 0}, {1, 1}, {0, 1});
  } else {
    ImVec2 uvMin{0.f, 0.f}, uvMax{1.f, 1.f};
    // Apply crop UV only when thumbnail is the original camera JPEG (not pre-cropped).
    if (mode_ == EditMode::Adjust && previewTex_ == nullptr && !thumbIsPreCropped_) {
      uvMin = {settings_.crop.x, settings_.crop.y};
      uvMax = {settings_.crop.x + settings_.crop.w, settings_.crop.y + settings_.crop.h};
    }
    dl->AddImage(reinterpret_cast<ImTextureID>(displayTex), imgMin, imgMax, uvMin, uvMax);
  }

  if (mode_ == EditMode::Crop) {
    renderCropOverlay(dl, imgMin, imgMax);
    handleCropDrag(imgMin, imgMax);
  }
}

// ── Background thumbnail save ─────────────────────────────────────────────────

void EditView::regenThumbnail(int64_t photoId,
                              catalog::EditSettings s,
                              std::vector<uint8_t> srcRgb,
                              int srcW, int srcH) {
  // Settings are persisted on the main thread via registry.dispatch("image.save")
  // in startSave() before this thread is launched.  Only thumbnail regen remains.

  if (srcRgb.empty()) {
    saveDone_ = true;
    return;
  }

  // Apply adjustments to the same LibRaw-decoded pixels used for the preview,
  // so the thumbnail matches both the preview and the exported JPEG exactly.
  auto edited = applyEditsToPixels(srcRgb, srcW, srcH, s);

  // Apply crop + straighten
  int cropW = 0, cropH = 0;
  auto cropped = util::cropAndRotatePixels(edited, srcW, srcH, s.crop, cropW, cropH);

  // Encode cropped+edited buffer to JPEG
  tjhandle tjEnc = tjInitCompress();
  if (!tjEnc) {
    saveDone_ = true;
    return;
  }
  uint8_t* jpegBuf = nullptr;
  unsigned long jpegSize = 0;
  if (tjCompress2(tjEnc, cropped.data(), cropW, 0, cropH, TJPF_RGB,
                  &jpegBuf, &jpegSize, TJSAMP_420, 85, TJFLAG_FASTDCT) < 0) {
    tjDestroy(tjEnc);
    saveDone_ = true;
    return;
  }
  tjDestroy(tjEnc);
  const std::vector<uint8_t> jpegVec(jpegBuf, jpegBuf + jpegSize);
  tjFree(jpegBuf);

  // Resize to thumbnail (max 256px)
  const auto thumbJpeg = catalog::ThumbnailCache::resizeJpeg(jpegVec, 256);
  if (thumbJpeg.empty()) {
    saveDone_ = true;
    return;
  }

  // Determine thumbnail dimensions from the cropped aspect ratio
  int thumbW, thumbH;
  if (cropW >= cropH) {
    thumbW = 256;
    thumbH = std::max(1, (int)(256.f * (float)cropH / (float)cropW));
  } else {
    thumbH = 256;
    thumbW = std::max(1, (int)(256.f * (float)cropW / (float)cropH));
  }

  // Write thumbnail file
  const std::string thumbDir = util::cacheDir() + "/thumbs_edit";
  std::error_code ec;
  fs::create_directories(thumbDir, ec);
  if (!fs::is_directory(thumbDir)) {
    spdlog::warn("EditView: cannot create thumb dir '{}': {}", thumbDir,
                 ec ? ec.message() : "not a directory");
    saveDone_ = true;
    return;
  }
  const std::string thumbPath = thumbDir + "/" + std::to_string(photoId) + ".jpg";
  {
    std::ofstream f(thumbPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(thumbJpeg.data()), (std::streamsize)thumbJpeg.size());
  }

  // Update DB under lock — edit_settings already saved; update thumb path.
  {
    std::lock_guard lk(repo_.db().mutex());
    repo_.updateThumb(photoId, thumbPath, thumbW, thumbH, 0);
  }
  saveDone_ = true;
}

void EditView::startSave() {
  if (originalRgb_.empty()) {
    return;
  }

  // Persist settings to DB and log the action via the command registry.
  // This runs on the main thread before the thumbnail-regen background thread starts,
  // so regenThumbnail() only needs to write the final thumb_path (not edit_settings).
  if (registry_) {
    const auto settingsJson = nlohmann::json::parse(settings_.toJson());
    registry_->dispatch("image.save", {{"id", photoId_}, {"settings", settingsJson}});
  } else {
    std::lock_guard lk(repo_.db().mutex());
    repo_.updateEditSettings(photoId_, settings_.toJson());
    repo_.updateThumb(photoId_, "", 0, 0, 0);
  }

  saving_ = true;
  saveDone_ = false;
  // Pass copies of the already-decoded LibRaw pixels so regenThumbnail uses
  // the same source as the preview — no camera-JPEG re-decode on the save thread.
  const catalog::EditSettings settingsCopy = settings_;
  const std::vector<uint8_t>  rgbCopy      = originalRgb_;
  const int                   wCopy        = srcW_;
  const int                   hCopy        = srcH_;
  saveThread_ = std::thread([this, settingsCopy, rgbCopy, wCopy, hCopy]() {
    regenThumbnail(photoId_, settingsCopy, rgbCopy, wCopy, hCopy);
  });
}

// ── render sub-steps ──────────────────────────────────────────────────────────

// Opens a transparent full-screen window that captures keyboard shortcuts.
// Returns true if the view was closed and the caller should return immediately.
bool EditView::handleKeyCapture(ImVec2 scr) {
  ImGui::SetNextWindowPos({0.f, 0.f});
  ImGui::SetNextWindowSize(scr);
  ImGui::SetNextWindowBgAlpha(0.f);
  ImGui::Begin("##editview", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);

  const bool skipKeys = justOpened_;
  justOpened_ = false;
  bool closed = false;
  if (!ImGui::GetIO().WantTextInput && !skipKeys) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      if (mode_ == EditMode::Crop) {
        settings_ = saved_;
        setMode(EditMode::Adjust);
      } else {
        settings_ = saved_;
        closed = true;
      }
    }
    if (mode_ == EditMode::Crop && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      startSave();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
      closed = true;
    }
  }
  ImGui::End();
  if (closed) {
    close();
  }
  return closed;
}

// Renders the preview image into the foreground draw list.
// In Crop mode, reserves space at the bottom for the straighten bar.
void EditView::renderPreviewArea(ImVec2 scr, float previewW) {
  constexpr float kStraightenBarH = 64.f;
  const float previewAreaH = (mode_ == EditMode::Crop) ? scr.y - kStraightenBarH : scr.y;
  ImDrawList* const fgDl = ImGui::GetForegroundDrawList();
  fgDl->AddRectFilled({0.f, 0.f}, {previewW, previewAreaH}, IM_COL32(0, 0, 0, 230));
  drawPreview(fgDl, {0.f, 0.f}, {previewW, previewAreaH});
  if (mode_ == EditMode::Crop) {
    renderStraightenBar(previewW, scr.y);
  }
}

// Renders the tab bar (Adjust / Crop) with their respective control panels.
void EditView::renderModeTabs() {
  if (!ImGui::BeginTabBar("##EditModeTab")) {
    return;
  }
  const bool syncAdj  = tabSyncNeeded_ && mode_ == EditMode::Adjust;
  const bool syncCrop = tabSyncNeeded_ && mode_ == EditMode::Crop;
  tabSyncNeeded_ = false;

  if (ImGui::BeginTabItem("Adjust", nullptr, syncAdj ? ImGuiTabItemFlags_SetSelected : 0)) {
    if (mode_ != EditMode::Adjust) { previewDirty_ = true; }
    mode_ = EditMode::Adjust;
    renderAdjustPanel();
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Crop", nullptr, syncCrop ? ImGuiTabItemFlags_SetSelected : 0)) {
    if (mode_ != EditMode::Crop) { previewDirty_ = true; }
    mode_ = EditMode::Crop;
    renderCropPanel();
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

// Renders pinned Save / Cancel buttons at the bottom of the control panel.
// Returns true if Cancel was pressed (caller must close the window first).
bool EditView::renderSaveButtons(ImVec2 scr) {
  ImGui::SetCursorPosY(scr.y - 52.f);
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::BeginDisabled(saving_ || fullDecoding_);
  if (ImGui::Button("Save", {130.f, 0.f})) {
    startSave();
  }
  ImGui::SameLine();
  const bool cancelled = ImGui::Button("Cancel", {130.f, 0.f});
  ImGui::EndDisabled();
  if (saving_) {
    ImGui::Text("Saving...");
  }
  if (cancelled) {
    settings_ = saved_;
    if (mode_ == EditMode::Crop) {
      setMode(EditMode::Adjust);
    } else {
      close();
    }
  }
  return cancelled;
}

// Positions and opens the right-hand control panel window, then renders its
// contents (mode tabs + save buttons).
// SetNextWindowFocus() is guarded by IsAnyItemActive() to avoid killing
// an in-progress slider drag on the straighten bar every frame.
void EditView::renderControlPanel(ImVec2 scr, float previewW) {
  const float panelW = scr.x - previewW;
  ImGui::SetNextWindowPos({previewW, 0.f});
  ImGui::SetNextWindowSize({panelW, scr.y});
  ImGui::SetNextWindowBgAlpha(0.95f);
  if (!ImGui::IsAnyItemActive()) {
    ImGui::SetNextWindowFocus();
  }
  ImGui::Begin("##editpanel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
  renderModeTabs();
  renderSaveButtons(scr);
  ImGui::End();
}

// Joins the save thread and fires the saved callback once the background
// thumbnail write completes.
void EditView::pollSaveCompletion() {
  if (!saving_ || !saveDone_.load()) {
    return;
  }
  saveThread_.join();
  saving_   = false;
  saveDone_ = false;
  pendingEvictId_ = photoId_;
  if (savedCb_) {
    savedCb_(photoId_);
  }
  if (mode_ == EditMode::Crop) {
    saved_ = settings_;
    setMode(EditMode::Adjust);
  } else {
    close();
  }
}

// ── render ────────────────────────────────────────────────────────────────────

void EditView::render() {
  if (!open_) { return; }

  pollLibRawLoad();

  const ImVec2  scr      = ImGui::GetIO().DisplaySize;
  const float   panelW   = 300.f;
  const float   previewW = scr.x - panelW;

  if (handleKeyCapture(scr)) { return; }
  renderPreviewArea(scr, previewW);
  renderControlPanel(scr, previewW);
  pollSaveCompletion();
}

}  // namespace ui
