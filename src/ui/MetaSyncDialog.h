#pragma once
#include "catalog/PhotoRepository.h"
#include "TextureManager.h"
#include <vector>
#include <cstdint>
#include <functional>
#include <string>

namespace command { class CommandRegistry; }

namespace ui {

class MetaSyncDialog {
 public:
  using DoneCb = std::function<void()>;

  // Edited thumbnail ready for upload — drained by main loop before grid.render()
  struct ThumbUpdate { int64_t id; std::vector<uint8_t> rgba; int w; int h; };

  MetaSyncDialog(catalog::PhotoRepository& repo, TextureManager& texMgr);

  void setDoneCallback(DoneCb cb) { doneCb_ = std::move(cb); }
  void setRegistry(const command::CommandRegistry* reg) { registry_ = reg; }

  // Open dialog: primaryId is the source photo; targetIds are all selected photos
  // (including primary — source settings propagate to all)
  void open(int64_t primaryId, std::vector<int64_t> targetIds);

  void render();
  bool isOpen() const { return open_; }

  // Called by main loop at the top of each frame (before grid.render()).
  // Returns pending texture updates produced by the last performSync().
  std::vector<ThumbUpdate> takePendingThumbUpdates();

 private:
  catalog::PhotoRepository& repo_;
  TextureManager& texMgr_;
  DoneCb doneCb_;
  const command::CommandRegistry* registry_ = nullptr;

  bool open_ = false;
  int64_t primaryId_ = 0;
  std::vector<int64_t> targetIds_;  // includes primaryId_ (all selected)
  std::string sourceFilename_;

  // Persisted checkbox state between opens (static so they survive re-open)
  bool syncAdjust_ = true;
  bool syncCrop_   = false;

  std::vector<ThumbUpdate> pendingThumbUpdates_;

  void performSync();
};

}  // namespace ui
