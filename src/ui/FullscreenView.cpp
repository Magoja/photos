#include "FullscreenView.h"
#include "ThumbCropUV.h"
#include "catalog/EditSettings.h"
#include "util/PixelPipeline.h"
#include <libraw/libraw.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <ranges>

namespace ui {

FullscreenView::FullscreenView(catalog::PhotoRepository& repo, TextureManager& texMgr)
  : repo_(repo), texMgr_(texMgr) {}

FullscreenView::~FullscreenView() {
  cancelDecode();
}

// ── Background decode ─────────────────────────────────────────────────────────

void FullscreenView::cancelDecode() {
  decodeCancel_ = true;
  if (decodeThread_.joinable()) {
    decodeThread_.join();
  }
  decodeCancel_ = false;
  decodeReady_  = false;
  decoding_     = false;
  pendingRgba_.clear();
}

void FullscreenView::startDecodeForCurrent() {
  // Pre-cropped thumbnails already contain correct tone from the save pipeline —
  // no decode needed; crop UV is also already applied (full {0,0,1,1}).
  const auto rec = repo_.findById(currentId_);
  if (!rec) { return; }
  if (rec->thumbPath.find("thumbs_edit") != std::string::npos) { return; }

  const std::string srcPath = repo_.fullPathFor(rec->folderId, rec->filename);
  if (srcPath.empty()) { return; }

  const catalog::EditSettings es = catalog::EditSettings::fromJson(rec->editSettings);
  decodingForId_ = currentId_;
  decoding_      = true;
  decodeReady_   = false;

  decodeThread_ = std::thread([this, srcPath, es]() {
    auto raw = std::make_unique<LibRaw>();
    raw->imgdata.params.output_bps    = 8;
    raw->imgdata.params.use_camera_wb = 1;
    if (raw->open_file(srcPath.c_str()) != LIBRAW_SUCCESS ||
        raw->unpack()                    != LIBRAW_SUCCESS ||
        raw->dcraw_process()             != LIBRAW_SUCCESS) {
      spdlog::warn("FullscreenView: LibRaw decode failed for {}", srcPath);
      decodeReady_ = true;
      return;
    }
    if (decodeCancel_.load()) { return; }

    libraw_processed_image_t* img = raw->dcraw_make_mem_image();
    if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3) {
      if (img) { LibRaw::dcraw_clear_mem(img); }
      decodeReady_ = true;
      return;
    }

    constexpr int kMaxEdge = 2000;
    const int scale = std::max(1, std::max(img->width, img->height) / kMaxEdge);
    int dsW = 0, dsH = 0;
    auto rgb = util::downsampleRgb(img->data, img->width, img->height, scale, dsW, dsH);
    LibRaw::dcraw_clear_mem(img);

    if (decodeCancel_.load()) { return; }

    const auto adjusted = util::applyAdjustments(rgb, dsW, dsH, es);
    pendingRgba_ = util::rgbToRgba(adjusted, dsW * dsH);
    pendingW_ = dsW;
    pendingH_ = dsH;

    if (!decodeCancel_.load()) {
      decodeReady_ = true;
    }
  });
}

void FullscreenView::pollDecodeResult() {
  if (!decoding_ || !decodeReady_.load()) { return; }
  decodeThread_.join();
  decoding_    = false;
  decodeReady_ = false;

  if (!pendingRgba_.empty()) {
    texMgr_.uploadRgba(decodingForId_ + kAdjOffset, pendingRgba_, pendingW_, pendingH_);
    pendingRgba_.clear();
  }
}

void FullscreenView::setPhotoList(std::vector<int64_t> ids, int64_t currentId) {
  photoIds_ = std::move(ids);
  currentId_ = currentId;
  const auto it = std::ranges::find(photoIds_, currentId);
  currentIdx_ = (it != photoIds_.end()) ? static_cast<int>(it - photoIds_.begin()) : 0;
}

void FullscreenView::open(int64_t photoId) {
  cancelDecode();
  texMgr_.evict(currentId_ + kAdjOffset);
  currentId_ = photoId;
  open_ = true;
  resetView();
  startDecodeForCurrent();
}

void FullscreenView::close() {
  cancelDecode();
  open_ = false;
}

void FullscreenView::resetView() {
  zoom_ = 1.f;
  panX_ = 0.f;
  panY_ = 0.f;
}

void FullscreenView::navigate(int delta) {
  if (photoIds_.empty()) {
    return;
  }
  cancelDecode();
  texMgr_.evict(currentId_ + kAdjOffset);
  currentIdx_ = std::clamp(currentIdx_ + delta, 0, (int)photoIds_.size() - 1);
  currentId_ = photoIds_[currentIdx_];
  resetView();
  startDecodeForCurrent();
}

// ── Input handlers ────────────────────────────────────────────────────────────

void FullscreenView::handleNavKeys() {
  if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_G)) {
    close();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    navigate(+1);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    navigate(-1);
  }

  if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent) && currentId_ > 0) {
    togglePickCurrentPhoto(currentId_);
  }

}

void FullscreenView::togglePickCurrentPhoto(const int64_t photoId) {
  const auto rec = repo_.findById(photoId);
  if (!rec) {
    return;
  }
  const int newPicked = rec->picked ? 0 : 1;
  repo_.updatePicked(photoId, newPicked);
  if (pickChangedCb_) {
    pickChangedCb_(photoId, newPicked);
  }
  toastText_ = newPicked ? "Picked" : "Unpicked";
  toastVisible_ = true;
  toastTimeLeft_ = 1.2f;
}

void FullscreenView::handleZoomAndPan(const ImGuiIO& io) {
  if (io.MouseWheel != 0.f) {
    zoom_ = std::clamp(zoom_ * (1.f + io.MouseWheel * 0.1f), 0.1f, 20.f);
  }
  if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    panX_ += io.MouseDelta.x;
    panY_ += io.MouseDelta.y;
  }
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

void FullscreenView::drawBackground(ImDrawList* dl, ImVec2 scrSz) const {
  dl->AddRectFilled({0, 0}, scrSz, IM_COL32(0, 0, 0, int(0.95f * 255)));
}

void FullscreenView::drawPhoto(ImDrawList* dl, ImVec2 scrSz) const {
  // Prefer the tone-adjusted texture (LibRaw decode + applyAdjustments) if available;
  // otherwise fall back to the cached camera-JPEG thumbnail as a loading placeholder.
  const auto adjTex = texMgr_.get(currentId_ + kAdjOffset);
  const bool hasAdj = adjTex && adjTex != texMgr_.placeholder();
  const auto tex = hasAdj ? adjTex : texMgr_.get(currentId_);
  if (!tex || tex == texMgr_.placeholder()) { return; }

  ImVec2 uvMin{0.f, 0.f}, uvMax{1.f, 1.f};
  float cropAspect = 1.f;
  const auto rec = repo_.findById(currentId_);
  if (rec) {
    ui::ThumbMeta m;
    m.preCropped = rec->thumbPath.find("thumbs_edit") != std::string::npos;
    if (!m.preCropped) {
      const auto es = catalog::EditSettings::fromJson(rec->editSettings);
      m.cropX = es.crop.x;
      m.cropY = es.crop.y;
      m.cropW = es.crop.w;
      m.cropH = es.crop.h;
    }
    const auto uv = ui::thumbCropUV(m);
    uvMin = {uv.u0, uv.v0};
    uvMax = {uv.u1, uv.v1};

    if (m.preCropped) {
      // Thumbnail already incorporates the crop — use its stored dimensions for
      // the correct letterbox aspect ratio.
      if (rec->thumbWidth > 0 && rec->thumbHeight > 0) {
        cropAspect = (float)rec->thumbWidth / (float)rec->thumbHeight;
      }
    } else {
      // Original camera JPEG — compute aspect from the crop region.
      if (rec->widthPx > 0 && rec->heightPx > 0) {
        cropAspect = (m.cropW * (float)rec->widthPx) /
                     (m.cropH * (float)rec->heightPx);
      } else {
        cropAspect = m.cropW / m.cropH;
      }
    }
  } else {
    const auto [texW, texH] = texMgr_.getSize(currentId_);
    if (texW > 0 && texH > 0) {
      cropAspect = (float)texW / (float)texH;
    }
  }

  // Letterbox: fit the crop region into the screen, then scale by zoom_.
  const float screenAspect = scrSz.x / scrSz.y;
  float baseW, baseH;
  if (cropAspect > screenAspect) {
    baseW = scrSz.x;
    baseH = baseW / cropAspect;
  } else {
    baseH = scrSz.y;
    baseW = baseH * cropAspect;
  }
  const float imgW = baseW * zoom_;
  const float imgH = baseH * zoom_;
  const float x = (scrSz.x - imgW) * 0.5f + panX_;
  const float y = (scrSz.y - imgH) * 0.5f + panY_;
  dl->AddImage(reinterpret_cast<ImTextureID>(tex), {x, y}, {x + imgW, y + imgH}, uvMin, uvMax);
}

void FullscreenView::drawStatusOverlay(ImDrawList* dl, ImVec2 scrSz) const {
  const auto rec = repo_.findById(currentId_);
  if (!rec) {
    return;
  }
  ImFont* const font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize();
  char info[256];
  std::snprintf(info, sizeof(info), "%s  |  %dx%d  |  %s  |  %s  [%d/%d]", rec->filename.c_str(),
                rec->widthPx, rec->heightPx, rec->captureTime.c_str(),
                rec->picked ? "★ Picked" : "", currentIdx_ + 1, (int)photoIds_.size());
  dl->AddText(font, fs, {10.f, scrSz.y - fs - 10.f}, IM_COL32_WHITE, info);
}

void FullscreenView::tickToast(ImDrawList* dl, ImVec2 scrSz, float dt) {
  toastTimeLeft_ -= dt;
  if (toastTimeLeft_ <= 0.f) {
    toastVisible_ = false;
    return;
  }

  const float alpha = std::min(1.f, toastTimeLeft_ / 0.4f);
  const ImU32 textCol = IM_COL32(255, 255, 255, int(alpha * 255));
  const ImU32 bgCol = IM_COL32(0, 0, 0, int(alpha * 0.6f * 255));
  ImFont* const font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize();

  const ImVec2 textSz = ImGui::CalcTextSize(toastText_.c_str());
  const float bw = textSz.x + 40.f;
  const float bh = textSz.y + 20.f;
  const float bx = (scrSz.x - bw) * 0.5f;
  const float by = scrSz.y * 0.35f - bh * 0.5f;

  dl->AddRectFilled({bx, by}, {bx + bw, by + bh}, bgCol, 8.f);
  dl->AddText(font, fs, {bx + 20.f, by + 10.f}, textCol, toastText_.c_str());
}

// ── render ────────────────────────────────────────────────────────────────────

void FullscreenView::render() {
  if (!open_) {
    return;
  }

  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 scrSz = io.DisplaySize;

  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(scrSz);
  ImGui::SetNextWindowBgAlpha(0.f);
  ImGui::Begin("##fullscreen", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);

  handleNavKeys();
  handleZoomAndPan(io);

  ImGui::End();

  pollDecodeResult();

  ImDrawList* const dl = ImGui::GetForegroundDrawList();
  drawBackground(dl, scrSz);
  drawPhoto(dl, scrSz);
  drawStatusOverlay(dl, scrSz);
  if (toastVisible_) {
    tickToast(dl, scrSz, io.DeltaTime);
  }
}

}  // namespace ui
