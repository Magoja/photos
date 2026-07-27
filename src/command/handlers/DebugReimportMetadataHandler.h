#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"

namespace command {

// debug.reimport.metadata — re-read EXIF/GPS metadata from every catalog photo's
// source file and write it back into the DB. Repairs rows imported before GPS/EXIF
// extraction worked. No pixels are re-decoded (metadata-only). Skips photos whose
// source file is missing (e.g. offline volume).
// Params: none.
// Result data: { "updated": int, "errors": int, "total": int }
class DebugReimportMetadataHandler : public ICommandHandler {
 public:
  explicit DebugReimportMetadataHandler(catalog::PhotoRepository& repo) : repo_(repo) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
};

}  // namespace command
