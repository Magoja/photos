#pragma once
#include "command/ICommandHandler.h"
#include "export/ExportSession.h"

namespace command {

// export.photos — start an async JPEG export for a set of photo ids.
// Params:
//   ids        : array of integers (required) — photos to export
//   targetPath : string (required)
//   quality    : integer (optional, default 90)
// Returns failure() if an export is already in progress.
// Returns success() immediately; export runs on a background thread.
class ExportHandler : public ICommandHandler {
 public:
  explicit ExportHandler(export_ns::ExportSession& session) : session_(session) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  export_ns::ExportSession& session_;
};

}  // namespace command
