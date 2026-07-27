#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"

namespace command {

// catalog.delete.folder — remove a folder from the catalog. Child folders and
// their photos cascade via ON DELETE CASCADE; cached thumbnail files for every
// photo in the subtree are deleted (kept if a surviving duplicate shares the hash).
// Params:
//   folderId : integer > 0 (required)
// Result data: { "folderId": id, "deleted": [photoId, ...] }
class CatalogDeleteFolderHandler : public ICommandHandler {
 public:
  explicit CatalogDeleteFolderHandler(catalog::PhotoRepository& repo) : repo_(repo) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
};

}  // namespace command
