#include "CatalogDeleteFolderHandler.h"
#include <filesystem>

namespace command {

namespace {

void removeThumbFile(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

// Delete a photo's thumbnail files only when no surviving row still references
// the same content hash (thumbnails are content-addressed and shared by dupes).
void removeThumbsIfUnshared(catalog::PhotoRepository& repo, const catalog::PhotoDeleteRef& ref) {
  if (!ref.fileHash.empty() && repo.findByHash(ref.fileHash)) {
    return;
  }
  removeThumbFile(ref.thumbPath);
  removeThumbFile(ref.thumbMicroPath);
}

nlohmann::json deletedIds(const std::vector<catalog::PhotoDeleteRef>& refs) {
  nlohmann::json ids = nlohmann::json::array();
  for (const auto& ref : refs) {
    ids.push_back(ref.id);
  }
  return ids;
}

}  // namespace

ValidationResult CatalogDeleteFolderHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("folderId") || !params["folderId"].is_number_integer()) {
    return invalid("missing required integer field 'folderId'");
  }
  if (params["folderId"].get<int64_t>() <= 0) {
    return invalid("'folderId' must be > 0");
  }
  return valid();
}

CommandResult CatalogDeleteFolderHandler::execute(nlohmann::json params) {
  const int64_t folderId = params["folderId"].get<int64_t>();

  const std::vector<catalog::PhotoDeleteRef> refs = repo_.photoRefsUnderFolder(folderId);
  repo_.deleteFolder(folderId);
  for (const auto& ref : refs) {
    removeThumbsIfUnshared(repo_, ref);
  }

  return success({{"folderId", folderId}, {"deleted", deletedIds(refs)}});
}

}  // namespace command
