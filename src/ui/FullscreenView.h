#pragma once
#include "TextureManager.h"
#include "catalog/PhotoRepository.h"
#include "imgui.h"
#include <vector>
#include <cstdint>
#include <functional>
#include <string>
#include <atomic>
#include <thread>

namespace ui {

class FullscreenView {
 public:
  using PickChangedCb = std::function<void(int64_t photoId, int picked)>;
  using OpenEditCb    = std::function<void(int64_t photoId)>;

  FullscreenView(catalog::PhotoRepository& repo, TextureManager& texMgr);
  ~FullscreenView();

  void setPickChangedCallback(PickChangedCb cb) { pickChangedCb_ = std::move(cb); }
  void setOpenEditCallback(OpenEditCb cb)        { openEditCb_    = std::move(cb); }

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
  OpenEditCb    openEditCb_;

  std::vector<int64_t> photoIds_;
  // Offset added to photoId when storing the tone-adjusted texture in TextureManager.
  // Must not collide with kMicroOffset (1'000'000'000).
  static constexpr int64_t kAdjOffset = 2'000'000'000LL;

  int64_t currentId_ = 0;
  int currentIdx_ = 0;
  bool open_ = false;

  float zoom_ = 1.f;
  float panX_ = 0.f;
  float panY_ = 0.f;

  // Background LibRaw decode for tone-correct display
  std::thread            decodeThread_;
  std::atomic<bool>      decodeCancel_{false};
  std::atomic<bool>      decodeReady_{false};
  bool                   decoding_ = false;
  std::vector<uint8_t>   pendingRgba_;
  int                    pendingW_ = 0;
  int                    pendingH_ = 0;
  int64_t                decodingForId_ = 0;

  // Toast state
  bool toastVisible_ = false;
  float toastTimeLeft_ = 0.f;
  std::string toastText_;

  void navigate(int delta);
  void resetView();
  void startDecodeForCurrent();
  void cancelDecode();
  void pollDecodeResult();

  void handleNavKeys();
  void togglePickCurrentPhoto(int64_t photoId);
  void handleZoomAndPan(const ImGuiIO& io);
  void drawBackground(ImDrawList* dl, ImVec2 scrSz) const;
  void drawPhoto(ImDrawList* dl, ImVec2 scrSz) const;
  void drawStatusOverlay(ImDrawList* dl, ImVec2 scrSz) const;
  void tickToast(ImDrawList* dl, ImVec2 scrSz, float dt);
};

}  // namespace ui
