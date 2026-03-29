#pragma once
#include "import/PreviewScanner.h"
#include "import/Importer.h"
#include "catalog/Database.h"
#include "ui/TextureManager.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_set>

namespace ui {

class ImportDialog {
 public:
  using DoneCb = std::function<void()>;

  ImportDialog(catalog::Database& db, TextureManager& texMgr);
  ~ImportDialog();

  void setDoneCallback(DoneCb cb) { doneCb_ = std::move(cb); }

  void open(const std::string& sourcePath, const std::string& destPath,
            const std::string& thumbCacheRoot);
  void close();
  bool isOpen() const { return open_; }

  void render();

 private:
  enum class State { kIdle, kScanning, kPreview, kImporting, kDone };

  void renderIdle();
  void renderScanning();
  void renderPreview();
  void renderImporting();
  void renderDone();
  void renderConflictModal();

  void startScan();
  void startImport();
  void drainScanQueue();
  void cancelAll();

  import_ns::ConflictResolution handleConflict(const std::string& filename,
                                               const std::string& destDir);
  void resolveConflict(import_ns::ConflictResolution res);

  catalog::Database& db_;
  TextureManager&    texMgr_;
  DoneCb             doneCb_;
  bool               open_ = false;

  State       state_      = State::kIdle;
  std::string sourcePath_;
  std::string destPath_;
  std::string thumbRoot_;
  bool        copyFiles_  = true;

  // Scanning phase
  std::unique_ptr<import_ns::PreviewScanner> scanner_;
  std::vector<import_ns::PreviewItem>        previewItems_;
  std::vector<import_ns::PreviewItem>        pendingItems_;  // background → main thread queue
  mutable std::mutex                         pendingMtx_;
  std::atomic<int>                           scanDone_{0};
  std::atomic<int>                           scanTotal_{0};
  int64_t                                    nextPreviewId_ = -1;

  // Preview selection
  std::unordered_set<int> selectedIndices_;
  int                     shiftAnchor_ = -1;

  // Import phase
  std::unique_ptr<import_ns::Importer> importer_;
  import_ns::ImportStats               stats_;
  std::atomic<int>                     totalFiles_{0};
  std::atomic<int>                     doneFiles_{0};
  std::mutex                           progressMtx_;
  std::string                          currentFile_;
  std::atomic<bool>                    importDone_{false};

  // Conflict handling (import thread blocks; main thread resolves)
  struct ConflictInfo { std::string filename; std::string destDir; };
  std::mutex                      conflictMtx_;
  std::condition_variable         conflictCv_;
  std::atomic<bool>               conflictPending_{false};
  ConflictInfo                    conflictInfo_;
  import_ns::ConflictResolution   conflictResolution_{import_ns::ConflictResolution::Skip};
  bool                            conflictResolved_ = true;
};

}  // namespace ui
