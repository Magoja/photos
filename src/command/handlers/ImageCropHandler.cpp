#include "ImageCropHandler.h"
#include "catalog/EditSettings.h"

namespace command {

ValidationResult ImageCropHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("id") || !params["id"].is_number_integer()) {
    return invalid("missing required integer field 'id'");
  }
  for (const char* field : {"x", "y", "w", "h", "angleDeg"}) {
    if (params.contains(field) && !params[field].is_number()) {
      return invalid(std::string("field '") + field + "' must be a number");
    }
  }
  return valid();
}

CommandResult ImageCropHandler::execute(nlohmann::json params) {
  const int64_t id = params["id"].get<int64_t>();
  const auto rec = repo_.findById(id);
  if (!rec) {
    return failure("photo not found: " + std::to_string(id));
  }

  auto s = catalog::EditSettings::fromJson(rec->editSettings);
  if (params.contains("x"))        { s.crop.x        = params["x"].get<float>(); }
  if (params.contains("y"))        { s.crop.y        = params["y"].get<float>(); }
  if (params.contains("w"))        { s.crop.w        = params["w"].get<float>(); }
  if (params.contains("h"))        { s.crop.h        = params["h"].get<float>(); }
  if (params.contains("angleDeg")) { s.crop.angleDeg = params["angleDeg"].get<float>(); }

  repo_.updateEditSettings(id, s.toJson());
  repo_.updateThumb(id, "", 0, 0, 0);
  return success();
}

}  // namespace command
