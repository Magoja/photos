#pragma once
#include "catalog/PhotoRepository.h"
#include "export/Exporter.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace export_ns {

// Owns the lifetime and progress state of one export run.
// Both ExportHandler (dispatch adapter) and ExportDialog (progress UI)
// share a reference to the same ExportSession — no post-construction setters needed.
class ExportSession {
 public:
  explicit ExportSession(catalog::PhotoRepository& repo);

  // Start async export. Returns false (no-op) if already running.
  bool start(std::vector<int64_t> ids, std::string targetPath, int quality = 90);

  void cancel();
  void reset();

  bool isRunning()     const;
  bool isFinished()    const { return finished_.load(); }
  int  doneCount()     const { return doneCount_.load(); }
  int  totalCount()    const { return totalCount_.load(); }
  int  exportedCount() const { return exportedCount_.load(); }
  int  errorCount()    const { return errorCount_.load(); }

 private:
  catalog::PhotoRepository&    repo_;
  std::unique_ptr<Exporter>    exporter_;

  std::atomic<bool> finished_{false};
  std::atomic<int>  doneCount_{0};
  std::atomic<int>  totalCount_{0};
  std::atomic<int>  exportedCount_{0};
  std::atomic<int>  errorCount_{0};
};

}  // namespace export_ns
