#pragma once
#include "catalog/PhotoRepository.h"
#include "TextureManager.h"
#include <vector>
#include <cstdint>
#include <functional>
#include <string>

namespace ui {

class MetaSyncDialog {
 public:
  using DoneCb = std::function<void()>;

  MetaSyncDialog(catalog::PhotoRepository& repo, TextureManager& texMgr);

  void setDoneCallback(DoneCb cb) { doneCb_ = std::move(cb); }

  // Open dialog: primaryId is the source photo; targetIds are all selected photos
  // (including primary — source settings propagate to all)
  void open(int64_t primaryId, std::vector<int64_t> targetIds);

  void render();
  bool isOpen() const { return open_; }

 private:
  catalog::PhotoRepository& repo_;
  TextureManager& texMgr_;
  DoneCb doneCb_;

  bool open_ = false;
  int64_t primaryId_ = 0;
  std::vector<int64_t> targetIds_;  // includes primaryId_ (all selected)
  std::string sourceFilename_;

  // Persisted checkbox state between opens (static so they survive re-open)
  bool syncAdjust_ = true;
  bool syncCrop_   = false;

  void performSync();
};

}  // namespace ui
