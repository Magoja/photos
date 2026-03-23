#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"
#include "export/Exporter.h"
#include <atomic>
#include <memory>

namespace command {

// export.photos — start an async JPEG export for a set of photo ids.
// Params:
//   ids        : array of integers (required) — photos to export
//   targetPath : string (required)
//   quality    : integer (optional, default 90)
// Returns failure() if an export is already in progress.
// Returns success() immediately; export runs on a background thread.
//
// Extra methods (used by ExportDialog for cancel / progress access):
//   isRunning(), isFinished(), cancel(), reset(),
//   doneCount(), totalCount(), exportedCount(), errorCount()
class ExportHandler : public ICommandHandler {
 public:
  ExportHandler(catalog::PhotoRepository& repo,
                export_ns::ProgressCb     progressCb,
                export_ns::DoneCb         doneCb)
      : repo_(repo),
        progressCb_(std::move(progressCb)),
        doneCb_(std::move(doneCb)) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

  // State access for ExportDialog
  bool isRunning()     const { return exporter_ && exporter_->isRunning(); }
  bool isFinished()    const { return finished_.load(); }
  void cancel()              { if (exporter_) { exporter_->cancel(); } }
  void reset();
  int  doneCount()     const { return doneCount_.load(); }
  int  totalCount()    const { return totalCount_.load(); }
  int  exportedCount() const { return exportedCount_.load(); }
  int  errorCount()    const { return errorCount_.load(); }

 private:
  catalog::PhotoRepository&    repo_;
  export_ns::ProgressCb        progressCb_;
  export_ns::DoneCb            doneCb_;
  std::unique_ptr<export_ns::Exporter> exporter_;

  std::atomic<bool> finished_{false};
  std::atomic<int>  doneCount_{0};
  std::atomic<int>  totalCount_{0};
  std::atomic<int>  exportedCount_{0};
  std::atomic<int>  errorCount_{0};
};

}  // namespace command
