#include "FullscreenView.h"
#include <algorithm>
#include <ranges>

namespace ui {

FullscreenView::FullscreenView(catalog::PhotoRepository& repo, TextureManager& texMgr)
  : repo_(repo), texMgr_(texMgr) {}

void FullscreenView::setPhotoList(std::vector<int64_t> ids, int64_t currentId) {
  photoIds_ = std::move(ids);
  currentId_ = currentId;
  auto it = std::ranges::find(photoIds_, currentId);
  currentIdx_ = (it != photoIds_.end()) ? static_cast<int>(it - photoIds_.begin()) : 0;
}

void FullscreenView::open(int64_t photoId) {
  currentId_ = photoId;
  open_ = true;
  resetView();
}

void FullscreenView::close() {
  open_ = false;
}

void FullscreenView::resetView() {
  zoom_ = 1.f;
  panX_ = 0.f;
  panY_ = 0.f;
}

void FullscreenView::navigate(int delta) {
  if (photoIds_.empty())
    return;
  currentIdx_ = std::clamp(currentIdx_ + delta, 0, (int)photoIds_.size() - 1);
  currentId_ = photoIds_[currentIdx_];
  resetView();
}

// ── Input handlers ────────────────────────────────────────────────────────────

void FullscreenView::handleNavKeys() {
  if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_G))
    close();
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    navigate(+1);
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    navigate(-1);

  if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent) && currentId_ > 0) {
    auto rec = repo_.findById(currentId_);
    if (rec) {
      int newPicked = rec->picked ? 0 : 1;
      repo_.updatePicked(currentId_, newPicked);
      if (pickChangedCb_)
        pickChangedCb_(currentId_, newPicked);
      toastText_ = newPicked ? "Picked" : "Unpicked";
      toastVisible_ = true;
      toastTimeLeft_ = 1.2f;
    }
  }
}

void FullscreenView::handleZoomAndPan(const ImGuiIO& io) {
  if (io.MouseWheel != 0.f)
    zoom_ = std::clamp(zoom_ * (1.f + io.MouseWheel * 0.1f), 0.1f, 20.f);
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
  void* tex = texMgr_.get(currentId_);
  float imgW = scrSz.x * zoom_;
  float imgH = scrSz.y * zoom_;
  float x = (scrSz.x - imgW) * 0.5f + panX_;
  float y = (scrSz.y - imgH) * 0.5f + panY_;
  dl->AddImage(reinterpret_cast<ImTextureID>(tex), {x, y}, {x + imgW, y + imgH});
}

void FullscreenView::drawStatusOverlay(ImDrawList* dl, ImVec2 scrSz) const {
  auto rec = repo_.findById(currentId_);
  if (!rec)
    return;
  ImFont* font = ImGui::GetFont();
  float fs = ImGui::GetFontSize();
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

  float alpha = std::min(1.f, toastTimeLeft_ / 0.4f);
  ImU32 textCol = IM_COL32(255, 255, 255, int(alpha * 255));
  ImU32 bgCol = IM_COL32(0, 0, 0, int(alpha * 0.6f * 255));
  ImFont* font = ImGui::GetFont();
  float fs = ImGui::GetFontSize();

  ImVec2 textSz = ImGui::CalcTextSize(toastText_.c_str());
  float bw = textSz.x + 40.f, bh = textSz.y + 20.f;
  float bx = (scrSz.x - bw) * 0.5f;
  float by = scrSz.y * 0.35f - bh * 0.5f;

  dl->AddRectFilled({bx, by}, {bx + bw, by + bh}, bgCol, 8.f);
  dl->AddText(font, fs, {bx + 20.f, by + 10.f}, textCol, toastText_.c_str());
}

// ── render ────────────────────────────────────────────────────────────────────

void FullscreenView::render() {
  if (!open_)
    return;

  ImGuiIO& io = ImGui::GetIO();
  ImVec2 scrSz = io.DisplaySize;

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

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  drawBackground(dl, scrSz);
  drawPhoto(dl, scrSz);
  drawStatusOverlay(dl, scrSz);
  if (toastVisible_)
    tickToast(dl, scrSz, io.DeltaTime);
}

}  // namespace ui
