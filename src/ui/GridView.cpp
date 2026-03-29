#include "GridView.h"
#include "ThumbCropUV.h"
#include "catalog/EditSettings.h"
#include "imgui.h"
#include <algorithm>
#include <ranges>

namespace ui {

GridView::GridView(catalog::PhotoRepository& repo, TextureManager& texMgr)
  : repo_(repo), texMgr_(texMgr) {}

void GridView::loadFolder(int64_t folderId, FilterMode filter) {
  folderId_ = folderId;
  filter_ = filter;
  primaryId_ = 0;
  selectedIds_.clear();
  reload();
}

void GridView::reload() {
  const bool pickedOnly = (filter_ == FilterMode::Picked);
  photoIds_ =
    (folderId_ == 0) ? repo_.queryAll(pickedOnly) : repo_.queryByFolder(folderId_, pickedOnly);
  requested_.clear();

  thumbMeta_.clear();
  const auto raw = repo_.queryThumbMeta(folderId_, pickedOnly);
  for (const auto& [id, paths] : raw) {
    const auto& [tPath, esJson] = paths;
    ThumbMeta m;
    m.preCropped = tPath.find("thumbs_edit") != std::string::npos;
    if (!m.preCropped) {
      const auto es = catalog::EditSettings::fromJson(esJson);
      m.cropX = es.crop.x;
      m.cropY = es.crop.y;
      m.cropW = es.crop.w;
      m.cropH = es.crop.h;
    }
    thumbMeta_[id] = m;
  }
}

void GridView::onThumbReady(int64_t photoId, const std::vector<uint8_t>& jpegBytes) {
  texMgr_.upload(photoId, jpegBytes);
}

// ── Selection helpers ──────────────────────────────────────────────────────────

void GridView::applyRangeSelect(int64_t fromId, int64_t toId) {
  const auto itA = std::ranges::find(photoIds_, fromId);
  const auto itB = std::ranges::find(photoIds_, toId);
  if (itA == photoIds_.end() || itB == photoIds_.end()) {
    return;
  }
  const auto lo = std::min(itA, itB);
  const auto hi = std::max(itA, itB);
  selectedIds_.clear();
  for (auto it = lo; it <= hi; ++it) {
    if (*it != fromId) {
      selectedIds_.insert(*it);
    }
  }
}

void GridView::handleCellClick(int64_t pid) {
  const auto& io = ImGui::GetIO();
  if (io.KeyShift && primaryId_ > 0) {
    applyRangeSelect(primaryId_, pid);
  } else if (io.KeyCtrl) {
    if (pid == primaryId_) {
      // Cmd+click on primary: promote first of selectedIds_ to primary
      if (!selectedIds_.empty()) {
        const int64_t next = *selectedIds_.begin();
        selectedIds_.erase(next);
        selectedIds_.insert(primaryId_);
        primaryId_ = next;
      } else {
        primaryId_ = 0;
      }
    } else if (selectedIds_.count(pid)) {
      selectedIds_.erase(pid);
    } else {
      if (primaryId_ == 0) {
        primaryId_ = pid;
      } else {
        selectedIds_.insert(pid);
      }
    }
  } else {
    // Plain click: clear multi-selection
    selectedIds_.clear();
    primaryId_ = pid;
    if (onSelectCb_) {
      onSelectCb_(pid);
    }
  }
}

// ── Navigation ────────────────────────────────────────────────────────────────

void GridView::navigatePrimary(int delta) {
  if (photoIds_.empty() || primaryId_ <= 0) { return; }
  const auto it = std::ranges::find(photoIds_, primaryId_);
  if (it == photoIds_.end()) { return; }
  const int idx  = static_cast<int>(std::distance(photoIds_.begin(), it));
  const int next = std::clamp(idx + delta, 0, static_cast<int>(photoIds_.size()) - 1);
  if (next == idx) { return; }
  selectedIds_.clear();
  primaryId_ = photoIds_[next];
  scrollToPrimary_ = true;
  if (onSelectCb_) { onSelectCb_(primaryId_); }
}

void GridView::selectAll() {
  if (photoIds_.empty()) { return; }
  selectedIds_.clear();
  primaryId_ = photoIds_.front();
  std::ranges::for_each(photoIds_ | std::views::drop(1),
    [this](const int64_t id) { selectedIds_.insert(id); });
}

void GridView::clearSelection() {
  primaryId_ = 0;
  selectedIds_.clear();
}

// ── Layout helpers ────────────────────────────────────────────────────────────

static std::pair<int, int> computeVisibleRowRange(int totalRows, float rowH, float scrollY,
                                                  float viewH) {
  int first = std::max(0, static_cast<int>(scrollY / rowH) - 1);
  int last = std::min(totalRows, static_cast<int>((scrollY + viewH) / rowH) + 2);
  return {first, last};
}

static std::pair<float, float> computeLetterboxSize(int tw, int th, float thumbW, float thumbH) {
  if (tw <= 0 || th <= 0) {
    return {thumbW, thumbH};
  }
  float aspect = (float)tw / (float)th;
  float cellAspect = thumbW / thumbH;
  if (aspect > cellAspect) {
    return {thumbW, thumbW / aspect};
  }
  return {thumbH * aspect, thumbH};
}

// ── render ────────────────────────────────────────────────────────────────────

void GridView::render() {
  // "N selected" label when multiple photos are selected
  const size_t totalSel = selectionCount();
  if (totalSel >= 2) {
    ImGui::TextColored({0.4f, 0.8f, 1.f, 1.f}, "%zu selected", totalSel);
    ImGui::SameLine();
    ImGui::Dummy({0.f, 0.f});
  }

  float thumbW = kThumbBase * thumbScale_;
  float thumbH = thumbW * (4.f / 6.f);
  float cellW = thumbW + kThumbPad * 2.f;
  float cellH = thumbH + kThumbPad * 2.f;

  float panelW = ImGui::GetContentRegionAvail().x;
  cols_ = std::max(1, static_cast<int>(panelW / cellW));
  const int cols = cols_;
  int totalRows = (static_cast<int>(photoIds_.size()) + cols - 1) / cols;

  // Scroll primary into viewport when navigatePrimary() requested it
  if (scrollToPrimary_ && primaryId_ > 0) {
    scrollToPrimary_ = false;
    const auto it = std::ranges::find(photoIds_, primaryId_);
    if (it != photoIds_.end()) {
      const int idx       = static_cast<int>(std::distance(photoIds_.begin(), it));
      const int targetRow = idx / cols_;
      const float targetY = targetRow * cellH;
      const float scrollY = ImGui::GetScrollY();
      const float viewH   = ImGui::GetContentRegionAvail().y;
      if (targetY < scrollY || targetY + cellH > scrollY + viewH) {
        ImGui::SetScrollY(targetY - viewH * 0.3f);
      }
    }
  }

  auto [firstRow, lastRow] =
    computeVisibleRowRange(totalRows, cellH, ImGui::GetScrollY(), ImGui::GetWindowHeight());

  if (firstRow > 0) {
    ImGui::Dummy({panelW, firstRow * cellH});
  }

  for (int row = firstRow; row < lastRow; ++row) {
    for (int col = 0; col < cols; ++col) {
      int idx = row * cols + col;
      if (idx >= (int)photoIds_.size()) {
        break;
      }

      int64_t pid = photoIds_[idx];
      const auto* stdTex   = texMgr_.get(pid);
      const auto* microTex = texMgr_.get(pid + kMicroOffset);
      const bool stdLoaded   = (stdTex   != texMgr_.placeholder());
      const bool microLoaded = (microTex != texMgr_.placeholder());

      if (!stdLoaded && thumbMissCb_ && !requested_.count(pid)) {
        requested_.insert(pid);
        thumbMissCb_(pid, repo_.getThumbPath(pid), repo_.getThumbMicroPath(pid));
      }

      const bool isPrimary  = (pid == primaryId_);
      const bool isOtherSel = (selectedIds_.count(pid) > 0);

      const auto* displayTex = stdLoaded   ? stdTex
                             : microLoaded ? microTex
                             : texMgr_.placeholder();
      const auto [tw, th] = stdLoaded   ? texMgr_.getSize(pid)
                          : microLoaded ? texMgr_.getSize(pid + kMicroOffset)
                          : std::pair{1, 1};
      const auto& meta = thumbMeta_.count(pid) ? thumbMeta_.at(pid) : ThumbMeta{};
      const auto [effW, effH] = stdLoaded
        ? std::pair{static_cast<int>(tw * meta.cropW), static_cast<int>(th * meta.cropH)}
        : std::pair{tw, th};
      auto [imgW, imgH] = computeLetterboxSize(effW, effH, thumbW, thumbH);

      if (col > 0) {
        ImGui::SameLine();
      }
      ImVec2 cellPos = ImGui::GetCursorScreenPos();

      ImGui::PushID(idx);
      bool clicked = ImGui::InvisibleButton("##cell", {cellW, cellH});

      ImDrawList* dl = ImGui::GetWindowDrawList();

      ImU32 bgCol = isPrimary  ? IM_COL32(80, 60, 10, 255)
                  : isOtherSel ? IM_COL32(10, 40, 80, 255)
                  : IM_COL32(35, 35, 35, 255);
      dl->AddRectFilled(cellPos, {cellPos.x + cellW, cellPos.y + cellH}, bgCol);

      float offX = (thumbW - imgW) * 0.5f + kThumbPad;
      float offY = (thumbH - imgH) * 0.5f + kThumbPad;
      ImVec2 imgMin = {cellPos.x + offX, cellPos.y + offY};
      ImVec2 imgMax = {imgMin.x + imgW, imgMin.y + imgH};
      const auto uv = thumbCropUV(meta);
      dl->AddImage(reinterpret_cast<ImTextureID>(displayTex), imgMin, imgMax,
                   {uv.u0, uv.v0}, {uv.u1, uv.v1});

      if (isPrimary) {
        dl->AddRect(cellPos, {cellPos.x + cellW, cellPos.y + cellH}, IM_COL32(255, 200, 50, 255),
                    0.f, 0, 2.f);
      } else if (isOtherSel) {
        dl->AddRect(cellPos, {cellPos.x + cellW, cellPos.y + cellH}, IM_COL32(80, 160, 255, 255),
                    0.f, 0, 2.f);
      }

      if (!repo_.libraryRootExists()) {
        ImVec2 badgePos = {cellPos.x + cellW - 18.f, cellPos.y + 2.f};
        dl->AddRectFilled(badgePos, {badgePos.x + 16.f, badgePos.y + 16.f},
                          IM_COL32(180, 40, 40, 220), 2.f);
        dl->AddText(badgePos, IM_COL32(255, 255, 255, 255), "?");
      }

      if (clicked) {
        handleCellClick(pid);
      }
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        handleCellClick(pid);
      }

      ImGui::PopID();
    }
  }

  int remainRows = totalRows - lastRow;
  if (remainRows > 0) {
    ImGui::Dummy({panelW, remainRows * cellH});
  }
}

}  // namespace ui
