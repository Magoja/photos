#include "CatalogDeletePhotosHandler.h"
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

std::vector<int64_t> idsFromParams(const nlohmann::json& params) {
  return params["ids"].get<std::vector<int64_t>>();
}

}  // namespace

ValidationResult CatalogDeletePhotosHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("ids") || !params["ids"].is_array()) {
    return invalid("missing required array field 'ids'");
  }
  if (params["ids"].empty()) {
    return invalid("'ids' must not be empty");
  }
  for (const auto& id : params["ids"]) {
    if (!id.is_number_integer()) {
      return invalid("'ids' must contain only integers");
    }
  }
  return valid();
}

CommandResult CatalogDeletePhotosHandler::execute(nlohmann::json params) {
  const std::vector<int64_t> ids = idsFromParams(params);

  const std::vector<catalog::PhotoDeleteRef> refs = repo_.photoRefsForIds(ids);
  repo_.deletePhotos(ids);
  for (const auto& ref : refs) {
    removeThumbsIfUnshared(repo_, ref);
  }

  return success({{"deleted", ids}});
}

}  // namespace command
