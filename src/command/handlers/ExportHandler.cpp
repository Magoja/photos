#include "ExportHandler.h"

namespace command {

ValidationResult ExportHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("ids") || !params["ids"].is_array()) {
    return invalid("missing required array field 'ids'");
  }
  if (!params.contains("targetPath") || !params["targetPath"].is_string()) {
    return invalid("missing required string field 'targetPath'");
  }
  return valid();
}

CommandResult ExportHandler::execute(nlohmann::json params) {
  if (isRunning()) {
    return failure("export already in progress");
  }

  const std::string targetPath = params["targetPath"].get<std::string>();
  const int         quality    = params.value("quality", 90);

  std::vector<int64_t> ids;
  ids.reserve(params["ids"].size());
  for (const auto& idJson : params["ids"]) {
    ids.push_back(idJson.get<int64_t>());
  }

  export_ns::ExportPreset preset;
  preset.name       = "Export";
  preset.quality    = quality;
  preset.targetPath = targetPath;

  finished_      = false;
  doneCount_     = 0;
  totalCount_    = static_cast<int>(ids.size());
  exportedCount_ = 0;
  errorCount_    = 0;

  exporter_ = std::make_unique<export_ns::Exporter>(repo_, preset);
  exporter_->setProgressCallback([this](const int done, const int total) {
    doneCount_  = done;
    totalCount_ = total;
    if (progressCb_) { progressCb_(done, total); }
  });
  exporter_->setDoneCallback([this](const int exp, const int err) {
    exportedCount_ = exp;
    errorCount_    = err;
    finished_      = true;
    if (doneCb_) { doneCb_(exp, err); }
  });
  exporter_->start(ids);
  return success();
}

void ExportHandler::reset() {
  cancel();
  exporter_.reset();
  finished_      = false;
  doneCount_     = 0;
  totalCount_    = 0;
  exportedCount_ = 0;
  errorCount_    = 0;
}

}  // namespace command
