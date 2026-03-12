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

  void open(int64_t photoId);
  void close();
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

  MTLTexturePtr previewTex_   = nullptr;
  bool          previewDirty_ = false;

  int    aspectMode_ = 0;  // 0=Free,1=1:1,2=2:3,3=3:2,4=4:5,5=16:9
  int    dragHandle_ = -1;
  ImVec2 dragStart_  = {};
  float  dragOrigX_ = 0.f, dragOrigY_ = 0.f;
  float  dragOrigW_ = 1.f, dragOrigH_ = 1.f;

  std::atomic<bool> saveDone_{false};
  bool              saving_    = false;
  std::thread       saveThread_;

  SavedCb savedCb_;

  // helpers
  bool loadSourcePixels(int64_t photoId);
  void rebuildPreviewTexture();
  void releasePreviewTex();

  std::vector<uint8_t> applyEditsToPixels(
      const std::vector<uint8_t>& src, int w, int h,
      const catalog::EditSettings& s) const;

  void applyCropConstraint();
  void renderCropOverlay(ImDrawList* dl, ImVec2 imgMin, ImVec2 imgMax) const;
  void handleCropDrag(ImVec2 imgMin, ImVec2 imgMax);

  void regenThumbnail(int64_t photoId,
                      catalog::EditSettings s,
                      std::string srcPath);

  void startSave();

  void renderAdjustPanel();
  void renderCropPanel();
  bool renderSliderRow(const char* label, float* v,
                       float vmin, float vmax, float step);
  void drawPreview(ImDrawList* dl, ImVec2 areaMin, ImVec2 areaMax);
};

}  // namespace ui
