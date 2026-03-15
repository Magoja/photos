// EditView.mm — non-destructive edit overlay (Adjust + Crop modes)
#import <Metal/Metal.h>

#include "EditView.h"
#include "import/RawDecoder.h"
#include "util/Platform.h"

#include <turbojpeg.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr std::array<AspectRatio, 7> kAspectRatios = {{
  {"Free"sv,     AspectConstraint::Free},
  {"Original"sv, AspectConstraint::Original},
  {"1:1"sv,      AspectConstraint::Fixed, 1.f/1.f},
  {"2:3"sv,      AspectConstraint::Fixed, 2.f/3.f},
  {"3:2"sv,      AspectConstraint::Fixed, 3.f/2.f},
  {"4:6"sv,      AspectConstraint::Fixed, 4.f/6.f},
  {"6:4"sv,      AspectConstraint::Fixed, 6.f/4.f},
}};
}  // namespace

namespace ui {

// ── Constructor / Destructor ──────────────────────────────────────────────────

EditView::EditView(catalog::PhotoRepository& repo,
                   catalog::ThumbnailCache&  thumbCache,
                   TextureManager&           texMgr,
                   MTLDevicePtr              device)
  : repo_(repo), thumbCache_(thumbCache), texMgr_(texMgr), device_(device) {}

EditView::~EditView() {
  releasePreviewTex();
  if (saveThread_.joinable()) {
    saveThread_.join();
  }
}

// ── open / close ─────────────────────────────────────────────────────────────

void EditView::open(int64_t photoId) {
  photoId_ = photoId;
  open_          = true;
  justOpened_    = true;
  mode_          = EditMode::Adjust;
  tabSyncNeeded_ = false;
  dragHandle_ = -1;
  aspectMode_ = 0;

  // Load existing edit settings from DB
  const auto rec = repo_.findById(photoId);
  if (rec) {
    settings_ = catalog::EditSettings::fromJson(rec->editSettings);
    origW_ = rec->widthPx;
    origH_ = rec->heightPx;
  } else {
    settings_ = {};
    origW_ = 0;
    origH_ = 0;
  }
  saved_ = settings_;

  loadSourcePixels(photoId);
  previewDirty_ = true;
}

void EditView::close() {
  open_ = false;
}

// ── Source pixel loading ──────────────────────────────────────────────────────

bool EditView::loadSourcePixels(int64_t photoId) {
  originalRgb_.clear();
  srcW_ = 0;
  srcH_ = 0;

  // Fast path: read thumb from disk
  std::vector<uint8_t> jpegBytes;
  const std::string thumbPath = repo_.getThumbPath(photoId);
  if (!thumbPath.empty()) {
    std::ifstream f(thumbPath, std::ios::binary);
    if (f) {
      jpegBytes.assign(std::istreambuf_iterator<char>(f), {});
    }
  }

  // Slow path: decode from source file
  if (jpegBytes.empty()) {
    const auto rec = repo_.findById(photoId);
    if (!rec) {
      return false;
    }
    const std::string srcPath = repo_.fullPathFor(rec->folderId, rec->filename);
    const auto dec = import_ns::RawDecoder::decode(srcPath);
    if (!dec.ok || dec.thumbJpeg.empty()) {
      return false;
    }
    jpegBytes = dec.thumbJpeg;
  }

  // Decompress JPEG → RGB
  tjhandle tj = tjInitDecompress();
  if (!tj) {
    return false;
  }
  int w = 0, h = 0, subsamp = 0, colorspace = 0;
  if (tjDecompressHeader3(tj, jpegBytes.data(), (unsigned long)jpegBytes.size(),
                          &w, &h, &subsamp, &colorspace) < 0) {
    tjDestroy(tj);
    return false;
  }
  std::vector<uint8_t> rgb(w * h * 3);
  if (tjDecompress2(tj, jpegBytes.data(), (unsigned long)jpegBytes.size(),
                    rgb.data(), w, 0, h, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
    tjDestroy(tj);
    return false;
  }
  tjDestroy(tj);
  originalRgb_ = std::move(rgb);
  srcW_ = w;
  srcH_ = h;
  return true;
}

// ── Pixel editing pipeline ────────────────────────────────────────────────────

std::vector<uint8_t> EditView::applyEditsToPixels(
    const std::vector<uint8_t>& src, int w, int h,
    const catalog::EditSettings& s) const {

  const float eMul  = std::pow(2.f, s.exposure);
  const float t     = s.temperature / 100.f;
  const float rMul  = 1.f + t * 0.30f;
  const float gMul  = 1.f + t * 0.05f;
  const float bMul  = 1.f - t * 0.30f;
  const float cFact = 1.f + s.contrast   / 100.f;
  const float sFact = 1.f + s.saturation / 100.f;

  std::vector<uint8_t> dst(src.size());
  const int n = w * h;
  for (int i = 0; i < n; ++i) {
    float r = src[i*3+0];
    float g = src[i*3+1];
    float b = src[i*3+2];

    // Exposure
    r *= eMul; g *= eMul; b *= eMul;
    // Color temperature
    r *= rMul; g *= gMul; b *= bMul;
    // Contrast (pivot = 128)
    r = 128.f + (r - 128.f) * cFact;
    g = 128.f + (g - 128.f) * cFact;
    b = 128.f + (b - 128.f) * cFact;
    // Saturation (BT.601 luma)
    const float L = 0.299f*r + 0.587f*g + 0.114f*b;
    r = L + (r - L) * sFact;
    g = L + (g - L) * sFact;
    b = L + (b - L) * sFact;

    dst[i*3+0] = (uint8_t)std::clamp(r, 0.f, 255.f);
    dst[i*3+1] = (uint8_t)std::clamp(g, 0.f, 255.f);
    dst[i*3+2] = (uint8_t)std::clamp(b, 0.f, 255.f);
  }
  return dst;
}

// ── Crop/rotate helpers ───────────────────────────────────────────────────────

static std::vector<uint8_t> rotateCropBuffer(const std::vector<uint8_t>& src,
                                              int w, int h, float angleDeg) {
  if (angleDeg == 0.f) {
    return src;
  }
  const float rad  = angleDeg * (float)M_PI / 180.f;
  const float cosA = std::cos(-rad);
  const float sinA = std::sin(-rad);
  const float cx   = w * 0.5f;
  const float cy   = h * 0.5f;

  std::vector<uint8_t> dst(w * h * 3, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float dx = x - cx;
      const float dy = y - cy;
      const float sx = cosA * dx - sinA * dy + cx;
      const float sy = sinA * dx + cosA * dy + cy;

      const int x0 = (int)sx;
      const int y0 = (int)sy;
      const float fx = sx - x0;
      const float fy = sy - y0;

      auto sample = [&](int px, int py) -> std::array<float, 3> {
        px = std::clamp(px, 0, w - 1);
        py = std::clamp(py, 0, h - 1);
        const int idx = (py * w + px) * 3;
        return {(float)src[idx], (float)src[idx+1], (float)src[idx+2]};
      };
      const auto s00 = sample(x0,   y0);
      const auto s10 = sample(x0+1, y0);
      const auto s01 = sample(x0,   y0+1);
      const auto s11 = sample(x0+1, y0+1);

      const int outIdx = (y * w + x) * 3;
      for (int c = 0; c < 3; ++c) {
        const float val = s00[c]*(1.f-fx)*(1.f-fy) + s10[c]*fx*(1.f-fy)
                        + s01[c]*(1.f-fx)*fy        + s11[c]*fx*fy;
        dst[outIdx+c] = (uint8_t)std::clamp(val, 0.f, 255.f);
      }
    }
  }
  return dst;
}

// Returns the cropped-and-rotated RGB buffer; outW/outH receive its dimensions.
static std::vector<uint8_t> cropAndRotatePixels(
    const std::vector<uint8_t>& edited, int srcW, int srcH,
    const catalog::CropRect& crop, int& outW, int& outH) {
  const int cropX = (int)(crop.x * srcW);
  const int cropY = (int)(crop.y * srcH);
  outW = std::max(1, (int)(crop.w * srcW));
  outH = std::max(1, (int)(crop.h * srcH));

  std::vector<uint8_t> cropped(outW * outH * 3);
  for (int y = 0; y < outH; ++y) {
    const int srcRow = std::clamp(cropY + y, 0, srcH - 1);
    const int dstOff = y * outW * 3;
    const int srcOff = (srcRow * srcW + std::clamp(cropX, 0, srcW - 1)) * 3;
    const int copyW  = std::min(outW, srcW - std::clamp(cropX, 0, srcW - 1));
    if (copyW > 0) {
      std::copy_n(edited.begin() + srcOff, copyW * 3, cropped.begin() + dstOff);
    }
  }
  if (crop.angleDeg != 0.f) {
    cropped = rotateCropBuffer(cropped, outW, outH, crop.angleDeg);
  }
  return cropped;
}

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
    croppedBuf = cropAndRotatePixels(edited, srcW_, srcH_, settings_.crop,
                                     previewW, previewH);
    pixels = &croppedBuf;
  }

  releasePreviewTex();

  // Convert RGB → RGBA
  std::vector<uint8_t> rgba;
  rgba.reserve(previewW * previewH * 4);
  for (int i = 0; i < previewW * previewH; ++i) {
    rgba.push_back((*pixels)[i*3+0]);
    rgba.push_back((*pixels)[i*3+1]);
    rgba.push_back((*pixels)[i*3+2]);
    rgba.push_back(255);
  }

  id<MTLDevice> dev = (id<MTLDevice>)device_;
  MTLTextureDescriptor* desc =
    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                       width:previewW
                                                      height:previewH
                                                   mipmapped:NO];
  desc.storageMode = MTLStorageModeShared;
  desc.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> tex = [dev newTextureWithDescriptor:desc];
  [tex replaceRegion:MTLRegionMake2D(0, 0, previewW, previewH)
         mipmapLevel:0
           withBytes:rgba.data()
         bytesPerRow:previewW * 4];
  previewTex_ = tex;
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

  // 8 handles
  const ImVec2 handles[8] = {
    {cx,      cy},       // TL
    {cx+cw/2, cy},       // T
    {cx+cw,   cy},       // TR
    {cx,      cy+ch/2},  // L
    {cx+cw,   cy+ch/2},  // R
    {cx,      cy+ch},    // BL
    {cx+cw/2, cy+ch},    // B
    {cx+cw,   cy+ch},    // BR
  };
  for (const auto& h : handles) {
    dl->AddRectFilled({h.x-6.f, h.y-6.f}, {h.x+6.f, h.y+6.f}, IM_COL32_WHITE);
  }

  // Horizontal center guideline — visible while the straighten slider is dragged
  if (straightenDragging_) {
    const float midY = (imgMin.y + imgMax.y) * 0.5f;
    dl->AddLine({imgMin.x, midY}, {imgMax.x, midY}, IM_COL32(255, 255, 100, 200), 1.f);
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

  const ImVec2 handles[8] = {
    {cx,      cy},
    {cx+cw/2, cy},
    {cx+cw,   cy},
    {cx,      cy+ch/2},
    {cx+cw,   cy+ch/2},
    {cx,      cy+ch},
    {cx+cw/2, cy+ch},
    {cx+cw,   cy+ch},
  };

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
  if (ImGui::SliderFloat("##straighten", &settings_.crop.angleDeg, -45.f, 45.f, "%.1f°")) {
    // no previewDirty_ — rotation applied via AddImageQuad each frame
  }
  straightenDragging_ = ImGui::IsItemActive();
  ImGui::PopStyleVar(2);
  ImGui::PopItemWidth();
  ImGui::SameLine(0.f, 12.f);
  if (ImGui::SmallButton("Reset")) {
    settings_.crop.angleDeg = 0.f;
    // no previewDirty_ — rotation applied via AddImageQuad each frame
  }

  ImGui::End();
}

// ── Preview drawing ───────────────────────────────────────────────────────────

void EditView::drawPreview(ImDrawList* dl, ImVec2 areaMin, ImVec2 areaMax) {
  if (previewDirty_) {
    rebuildPreviewTexture();
    previewDirty_ = false;
  }
  if (!previewTex_ || srcW_ <= 0 || srcH_ <= 0) {
    return;
  }

  const float areaW = (areaMax.x - areaMin.x) * 0.9f;
  const float areaH = (areaMax.y - areaMin.y) * 0.9f;
  const float imgAspect  = (float)previewTex_.width / (float)previewTex_.height;
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
    const float angleRad = settings_.crop.angleDeg * (float)(M_PI / 180.0);
    const float cosA = cosf(angleRad), sinA = sinf(angleRad);
    const ImVec2 center = {(imgMin.x + imgMax.x) * 0.5f,
                           (imgMin.y + imgMax.y) * 0.5f};
    const auto rotPt = [&](float px, float py) -> ImVec2 {
      const float dx = px - center.x, dy = py - center.y;
      return {center.x + dx * cosA - dy * sinA,
              center.y + dx * sinA + dy * cosA};
    };
    dl->AddImageQuad(reinterpret_cast<ImTextureID>(previewTex_),
                     rotPt(imgMin.x, imgMin.y), rotPt(imgMax.x, imgMin.y),
                     rotPt(imgMax.x, imgMax.y), rotPt(imgMin.x, imgMax.y),
                     {0, 0}, {1, 0}, {1, 1}, {0, 1});
  } else {
    dl->AddImage(reinterpret_cast<ImTextureID>(previewTex_), imgMin, imgMax);
  }

  if (mode_ == EditMode::Crop) {
    renderCropOverlay(dl, imgMin, imgMax);
    handleCropDrag(imgMin, imgMax);
  }
}

// ── Background thumbnail save ─────────────────────────────────────────────────

void EditView::regenThumbnail(int64_t photoId,
                              catalog::EditSettings s,
                              std::string srcPath) {
  // Decode source file's embedded JPEG
  const auto dec = import_ns::RawDecoder::decode(srcPath);
  if (!dec.ok || dec.thumbJpeg.empty()) {
    spdlog::warn("EditView::regenThumbnail: decode failed for {}", srcPath);
    saveDone_ = true;
    return;
  }

  // Decompress to RGB
  tjhandle tj = tjInitDecompress();
  if (!tj) {
    saveDone_ = true;
    return;
  }
  int srcW = 0, srcH = 0, subsamp = 0, colorspace = 0;
  if (tjDecompressHeader3(tj, dec.thumbJpeg.data(), (unsigned long)dec.thumbJpeg.size(),
                          &srcW, &srcH, &subsamp, &colorspace) < 0) {
    tjDestroy(tj);
    saveDone_ = true;
    return;
  }
  std::vector<uint8_t> rgb(srcW * srcH * 3);
  if (tjDecompress2(tj, dec.thumbJpeg.data(), (unsigned long)dec.thumbJpeg.size(),
                    rgb.data(), srcW, 0, srcH, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
    tjDestroy(tj);
    saveDone_ = true;
    return;
  }
  tjDestroy(tj);

  // Apply adjustments
  auto edited = applyEditsToPixels(rgb, srcW, srcH, s);

  // Apply crop + straighten
  int cropW = 0, cropH = 0;
  auto cropped = cropAndRotatePixels(edited, srcW, srcH, s.crop, cropW, cropH);

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
  fs::create_directories(thumbDir);
  const std::string thumbPath = thumbDir + "/" + std::to_string(photoId) + ".jpg";
  {
    std::ofstream f(thumbPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(thumbJpeg.data()), (std::streamsize)thumbJpeg.size());
  }

  // Update DB under lock
  {
    std::lock_guard lk(repo_.db().mutex());
    repo_.updateEditSettings(photoId, s.toJson());
    repo_.updateThumb(photoId, thumbPath, thumbW, thumbH, 0);
  }
  saveDone_ = true;
}

void EditView::startSave() {
  const auto rec = repo_.findById(photoId_);
  if (!rec) {
    return;
  }
  const std::string srcPath = repo_.fullPathFor(rec->folderId, rec->filename);
  saving_ = true;
  saveDone_ = false;
  const catalog::EditSettings settingsCopy = settings_;
  saveThread_ = std::thread([this, srcPath, settingsCopy]() {
    regenThumbnail(photoId_, settingsCopy, srcPath);
  });
}

// ── render ────────────────────────────────────────────────────────────────────

void EditView::render() {
  if (!open_) {
    return;
  }

  const ImGuiIO& io  = ImGui::GetIO();
  const ImVec2   scr = io.DisplaySize;
  const float    panelW = 300.f;
  const float    previewW = scr.x - panelW;

  // Full-screen capture window (transparent background, handles keys)
  ImGui::SetNextWindowPos({0.f, 0.f});
  ImGui::SetNextWindowSize(scr);
  ImGui::SetNextWindowBgAlpha(0.f);
  ImGui::Begin("##editview", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);

  const bool skipKeys = justOpened_;
  justOpened_ = false;
  if (!io.WantTextInput && !skipKeys) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      settings_ = saved_;
      ImGui::End();
      close();
      return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_D)) {
      ImGui::End();
      close();
      return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
      mode_ = (mode_ == EditMode::Adjust) ? EditMode::Crop : EditMode::Adjust;
      tabSyncNeeded_ = true;
    }
  }
  ImGui::End();

  // Overlay + preview drawn to foreground list — renders above all ImGui windows.
  // In Crop mode, reserve the bottom 64 px for the straighten bar.
  constexpr float kStraightenBarH = 64.f;
  const float previewAreaH = (mode_ == EditMode::Crop) ? scr.y - kStraightenBarH : scr.y;
  ImDrawList* const fgDl = ImGui::GetForegroundDrawList();
  fgDl->AddRectFilled({0.f, 0.f}, {previewW, previewAreaH}, IM_COL32(0, 0, 0, 230));
  drawPreview(fgDl, {0.f, 0.f}, {previewW, previewAreaH});

  if (mode_ == EditMode::Crop) {
    renderStraightenBar(previewW, scr.y);
  }

  // Right control panel — forced to front each frame, but only when no item is
  // being dragged.  SetNextWindowFocus() → FocusWindow() → SetActiveID(0) which
  // would kill an in-progress slider drag on the straighten bar every frame.
  ImGui::SetNextWindowPos({previewW, 0.f});
  ImGui::SetNextWindowSize({panelW, scr.y});
  ImGui::SetNextWindowBgAlpha(0.95f);
  if (!ImGui::IsAnyItemActive()) {
    ImGui::SetNextWindowFocus();
  }
  ImGui::Begin("##editpanel", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

  if (ImGui::BeginTabBar("##EditModeTab")) {
    const bool syncAdj  = tabSyncNeeded_ && mode_ == EditMode::Adjust;
    const bool syncCrop = tabSyncNeeded_ && mode_ == EditMode::Crop;
    tabSyncNeeded_ = false;

    if (ImGui::BeginTabItem("Adjust", nullptr,
                            syncAdj ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (mode_ != EditMode::Adjust) { previewDirty_ = true; }
      mode_ = EditMode::Adjust;
      renderAdjustPanel();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Crop", nullptr,
                            syncCrop ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (mode_ != EditMode::Crop) { previewDirty_ = true; }
      mode_ = EditMode::Crop;
      renderCropPanel();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  // Save / Cancel buttons pinned near bottom
  ImGui::SetCursorPosY(scr.y - 52.f);
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::BeginDisabled(saving_);
  if (ImGui::Button("Save", {130.f, 0.f})) {
    startSave();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", {130.f, 0.f})) {
    settings_ = saved_;
    ImGui::EndDisabled();
    ImGui::End();
    close();
    return;
  }
  ImGui::EndDisabled();
  if (saving_) {
    ImGui::Text("Saving...");
  }

  ImGui::End();

  // Poll save completion
  if (saving_ && saveDone_.load()) {
    saveThread_.join();
    saving_ = false;
    saveDone_ = false;
    pendingEvictId_ = photoId_;
    if (savedCb_) {
      savedCb_(photoId_);
    }
    close();
  }
}

}  // namespace ui
