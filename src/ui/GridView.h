#pragma once
#include "TextureManager.h"
#include "FilterBar.h"
#include "ThumbCropUV.h"
#include "catalog/PhotoRepository.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <string>

namespace ui {

class GridView {
 public:
  using SelectCb = std::function<void(int64_t photoId)>;
  using PickCb = std::function<void(int64_t photoId, int picked)>;
  using ThumbMissCb =
    std::function<void(int64_t photoId, std::string thumbPath, std::string microPath)>;
  using MultiSelectCb = std::function<void(std::vector<int64_t>)>;

  static constexpr int64_t kMicroOffset = 1'000'000'000LL;

  GridView(catalog::PhotoRepository& repo, TextureManager& texMgr);

  void setOnSelect(SelectCb cb) { onSelectCb_ = std::move(cb); }
  void setOnPick(PickCb cb) { onPickCb_ = std::move(cb); }
  void setThumbMissCallback(ThumbMissCb cb) { thumbMissCb_ = std::move(cb); }
  void setOnMultiSelect(MultiSelectCb cb) { onMultiSelectCb_ = std::move(cb); }

  // Reload photo IDs for given folder (0 = all)
  void loadFolder(int64_t folderId, FilterMode filter);
  void reload();

  // Render the grid inside the current ImGui window
  void render();

  // Move primary selection by delta cells (±1 for left/right, ±cols_ for up/down)
  void navigatePrimary(int delta);

  int columnCount() const { return cols_; }

  // Primary selection (compat: replaces old selectedId())
  int64_t selectedId() const { return primaryId_; }
  int64_t primaryId() const { return primaryId_; }

  // Multi-selection: photos selected via Cmd+click / Shift+click (excludes primaryId_)
  const std::unordered_set<int64_t>& selectedIds() const { return selectedIds_; }

  // Total selected photos (primary + additional); 0 if nothing selected
  size_t selectionCount() const { return (primaryId_ > 0 ? 1 : 0) + selectedIds_.size(); }

  size_t photoCount() const { return photoIds_.size(); }

  // Async: call from main thread when a texture decode completes
  void onThumbReady(int64_t photoId, const std::vector<uint8_t>& jpegBytes);

 private:
  catalog::PhotoRepository& repo_;
  TextureManager& texMgr_;
  SelectCb onSelectCb_;
  PickCb onPickCb_;
  ThumbMissCb thumbMissCb_;
  MultiSelectCb onMultiSelectCb_;

  std::vector<int64_t> photoIds_;
  std::unordered_set<int64_t> requested_;  // IDs for which thumb load was requested
  int64_t folderId_ = 0;
  FilterMode filter_ = FilterMode::All;

  int64_t primaryId_ = 0;                    // anchor / primary selection
  std::unordered_set<int64_t> selectedIds_;  // additional Cmd/Shift selected (excludes primary)

  int cols_ = 1;                // updated each render(); used by navigatePrimary
  bool scrollToPrimary_ = false; // set by navigatePrimary; consumed by render()

  std::unordered_map<int64_t, ui::ThumbMeta> thumbMeta_;  // per-photo crop/pre-crop flags

  static constexpr float kThumbBase = 120.f;
  static constexpr float kThumbPad = 4.f;

  float thumbScale_ = 1.f;

  void handleCellClick(int64_t pid);
  void applyRangeSelect(int64_t fromId, int64_t toId);

 public:
  float thumbScale() const { return thumbScale_; }
  void setThumbScale(float s) { thumbScale_ = s; }
};

}  // namespace ui
