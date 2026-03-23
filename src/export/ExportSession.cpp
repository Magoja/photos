#include "export/ExportSession.h"
#include "export/ExportPreset.h"

namespace export_ns {

ExportSession::ExportSession(catalog::PhotoRepository& repo) : repo_(repo) {}

bool ExportSession::isRunning() const {
  return exporter_ && exporter_->isRunning();
}

bool ExportSession::start(std::vector<int64_t> ids, std::string targetPath, int quality) {
  if (isRunning()) { return false; }

  ExportPreset preset;
  preset.name       = "Export";
  preset.quality    = quality;
  preset.targetPath = std::move(targetPath);

  finished_      = false;
  doneCount_     = 0;
  totalCount_    = static_cast<int>(ids.size());
  exportedCount_ = 0;
  errorCount_    = 0;

  exporter_ = std::make_unique<Exporter>(repo_, preset);
  exporter_->setProgressCallback([this](const int done, const int total) {
    doneCount_  = done;
    totalCount_ = total;
  });
  exporter_->setDoneCallback([this](const int exp, const int err) {
    exportedCount_ = exp;
    errorCount_    = err;
    finished_      = true;
  });
  exporter_->start(ids);
  return true;
}

void ExportSession::cancel() {
  if (exporter_) { exporter_->cancel(); }
}

void ExportSession::reset() {
  cancel();
  exporter_.reset();
  finished_      = false;
  doneCount_     = 0;
  totalCount_    = 0;
  exportedCount_ = 0;
  errorCount_    = 0;
}

}  // namespace export_ns
