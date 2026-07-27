#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"

namespace command {

// catalog.delete.photos — remove photo rows from the catalog and delete their
// cached thumbnail files (a thumb file is kept if a surviving duplicate still
// references the same content hash).
// Params:
//   ids : non-empty array of integers (required)
// Result data: { "deleted": [id, ...] }
class CatalogDeletePhotosHandler : public ICommandHandler {
 public:
  explicit CatalogDeletePhotosHandler(catalog::PhotoRepository& repo) : repo_(repo) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
};

}  // namespace command
