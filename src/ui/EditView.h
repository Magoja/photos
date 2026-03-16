#pragma once
#include "catalog/PhotoRepository.h"
#include "catalog/ThumbnailCache.h"
#include "catalog/EditSettings.h"
#include "TextureManager.h"
#include "imgui.h"
#include <vector>
#include <cstdint>
#include <functional>
#include <string>
#include <atomic>
#include <thread>

#ifdef __OBJC__
#import <Metal/Metal.h>
using MTLTexturePtr = id<MTLTexture>;
using MTLDevicePtr  = id<MTLDevice>;
#else
using MTLTexturePtr = void*;
using MTLDevicePtr  = void*;
#endif

namespace ui {

enum class EditMode { Adjust, Crop };

class EditView {
 public:
  using SavedCb = std::function<void(int64_t photoId)>;

  EditView(catalog::PhotoRepository& repo,
           catalog::ThumbnailCache&  thumbCache,
           TextureManager&           texMgr,
           MTLDevicePtr              device);
  ~EditView();

  void setSavedCallback(SavedCb cb) { savedCb_ = std::move(cb); }

  // Returns the photoId whose LRU texture should be evicted this frame, or 0 if none.
  // Must be called before any grid AddImage calls to avoid use-after-free.
  int64_t pollPendingEvict() noexcept {
    const int64_t id = pendingEvictId_;
    pendingEvictId_ = 0;
    return id;
  }

  void open(int64_t photoId);
  void close();
  void setMode(EditMode mode);
  bool isOpen() const { return open_; }

  void render();

 private:
  catalog::PhotoRepository& repo_;
  catalog::ThumbnailCache&  thumbCache_;
  TextureManager&           texMgr_;
  MTLDevicePtr              device_;

  bool     open_        = false;
  bool     justOpened_  = false;
  int64_t  photoId_     = 0;
  EditMode mode_        = EditMode::Adjust;
  bool     tabSyncNeeded_ = false;

  catalog::EditSettings settings_;  // live/working values
  catalog::EditSettings saved_;     // baseline for Cancel

  std::vector<uint8_t> originalRgb_;
  int srcW_ = 0, srcH_ = 0;
  int origW_ = 0, origH_ = 0;  // original photo dimensions from EXIF (DB widthPx/heightPx)

  MTLTexturePtr previewTex_   = nullptr;
  bool          previewDirty_ = false;

  int    aspectMode_ = 0;  // 0=Free,1=Original,2=1:1,3=2:3,4=3:2
  bool   straightenDragging_ = false;
  int    dragHandle_ = -1;
  ImVec2 dragStart_  = {};
  float  dragOrigX_ = 0.f, dragOrigY_ = 0.f;
  float  dragOrigW_ = 1.f, dragOrigH_ = 1.f;

  std::atomic<bool> saveDone_{false};
  bool              saving_    = false;
  std::thread       saveThread_;

  SavedCb savedCb_;
  int64_t pendingEvictId_ = 0;

  // helpers
  bool loadSourcePixels(int64_t photoId);
  void rebuildPreviewTexture();
  void releasePreviewTex();

  std::vector<uint8_t> applyEditsToPixels(
      const std::vector<uint8_t>& src, int w, int h,
      const catalog::EditSettings& s) const;

  void applyCropConstraint(int handle);
  void renderCropOverlay(ImDrawList* dl, ImVec2 imgMin, ImVec2 imgMax) const;
  void handleCropDrag(ImVec2 imgMin, ImVec2 imgMax);

  void regenThumbnail(int64_t photoId,
                      catalog::EditSettings s,
                      std::string srcPath);

  void startSave();

  // render sub-steps (called in order from render())
  bool handleKeyCapture(ImVec2 scr);
  void renderPreviewArea(ImVec2 scr, float previewW);
  void renderControlPanel(ImVec2 scr, float previewW);
  void renderModeTabs();
  bool renderSaveButtons(ImVec2 scr);
  void pollSaveCompletion();

  void renderAdjustPanel();
  void renderCropPanel();
  void renderStraightenBar(float previewW, float screenH);
  bool renderSliderRow(const char* label, float* v,
                       float vmin, float vmax, float step);
  void drawPreview(ImDrawList* dl, ImVec2 areaMin, ImVec2 areaMax);
};

}  // namespace ui
