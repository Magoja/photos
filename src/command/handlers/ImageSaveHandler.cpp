#include "ImageSaveHandler.h"
#include "catalog/EditSettings.h"

namespace command {

ValidationResult ImageSaveHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("id") || !params["id"].is_number_integer()) {
    return invalid("missing required integer field 'id'");
  }
  if (!params.contains("settings") || !params["settings"].is_object()) {
    return invalid("missing required object field 'settings'");
  }
  return valid();
}

CommandResult ImageSaveHandler::execute(nlohmann::json params) {
  const int64_t id = params["id"].get<int64_t>();
  const auto rec = repo_.findById(id);
  if (!rec) {
    return failure("photo not found: " + std::to_string(id));
  }

  const auto s = catalog::EditSettings::fromJson(params["settings"].dump());
  repo_.updateEditSettings(id, s.toJson());
  repo_.updateThumb(id, "", 0, 0, 0);
  if (savedCb_) { savedCb_(id); }
  return success();
}

}  // namespace command
