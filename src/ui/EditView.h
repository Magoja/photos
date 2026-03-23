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

namespace command { class CommandRegistry; }

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
  void setRegistry(command::CommandRegistry* reg) { registry_ = reg; }

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

  bool     open_             = false;
  bool     justOpened_       = false;
  bool     thumbIsPreCropped_ = false;
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

  // Two-phase load: grid texture shown first, LibRaw accurate decode in background
  std::atomic<bool> fullDecodeReady_{false};   // set by loadThread_ when done
  std::atomic<bool> fullDecodeCancel_{false};  // set by main thread to abort
  bool              fullDecoding_ = false;     // main-thread flag (mirrors thread state)
  std::thread       loadThread_;
  std::vector<uint8_t> pendingRgb_;            // written by loadThread_, swapped on main
  int               pendingW_ = 0, pendingH_ = 0;
  // Retained strong ref to the grid thumbnail used as fallback while LibRaw decodes.
  // Prevents the LRU from freeing the MTLTexture between draw-list population and
  // ImGui_ImplMetal_RenderDrawData (which would cause a dangling-pointer crash).
  MTLTexturePtr     fallbackTex_ = nullptr;

  std::atomic<bool> saveDone_{false};
  bool              saving_    = false;
  std::thread       saveThread_;

  SavedCb savedCb_;
  int64_t pendingEvictId_ = 0;
  command::CommandRegistry* registry_ = nullptr;

  // helpers
  void loadLibRawBackground(std::string srcPath);
  void pollLibRawLoad();
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
                      std::vector<uint8_t> srcRgb,
                      int srcW, int srcH);

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
