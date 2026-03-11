#pragma once
#include "TextureManager.h"
#include "catalog/PhotoRepository.h"
#include "imgui.h"
#include <vector>
#include <cstdint>
#include <functional>
#include <string>

namespace ui {

class FullscreenView {
 public:
  using PickChangedCb = std::function<void(int64_t photoId, int picked)>;

  FullscreenView(catalog::PhotoRepository& repo, TextureManager& texMgr);

  void setPickChangedCallback(PickChangedCb cb) { pickChangedCb_ = std::move(cb); }

  // Set the list of photo IDs to navigate through
  void setPhotoList(std::vector<int64_t> ids, int64_t currentId);

  // Open/close
  void open(int64_t photoId);
  void close();
  bool isOpen() const { return open_; }

  // Render the fullscreen overlay; handles keyboard input
  void render();

  int64_t currentId() const { return currentId_; }

 private:
  catalog::PhotoRepository& repo_;
  TextureManager& texMgr_;
  PickChangedCb pickChangedCb_;

  std::vector<int64_t> photoIds_;
  int64_t currentId_ = 0;
  int currentIdx_ = 0;
  bool open_ = false;

  float zoom_ = 1.f;
  float panX_ = 0.f;
  float panY_ = 0.f;

  // Toast state
  bool toastVisible_ = false;
  float toastTimeLeft_ = 0.f;
  std::string toastText_;

  void navigate(int delta);
  void resetView();

  void handleNavKeys();
  void togglePickCurrentPhoto(int64_t photoId);
  void handleZoomAndPan(const ImGuiIO& io);
  void drawBackground(ImDrawList* dl, ImVec2 scrSz) const;
  void drawPhoto(ImDrawList* dl, ImVec2 scrSz) const;
  void drawStatusOverlay(ImDrawList* dl, ImVec2 scrSz) const;
  void tickToast(ImDrawList* dl, ImVec2 scrSz, float dt);
};

}  // namespace ui
