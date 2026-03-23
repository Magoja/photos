#include "ImageAdjustHandler.h"
#include "catalog/EditSettings.h"

namespace command {

ValidationResult ImageAdjustHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("id") || !params["id"].is_number_integer()) {
    return invalid("missing required integer field 'id'");
  }
  for (const char* field : {"exposure", "temperature", "contrast", "saturation"}) {
    if (params.contains(field) && !params[field].is_number()) {
      return invalid(std::string("field '") + field + "' must be a number");
    }
  }
  return valid();
}

CommandResult ImageAdjustHandler::execute(nlohmann::json params) {
  const int64_t id = params["id"].get<int64_t>();
  const auto rec = repo_.findById(id);
  if (!rec) {
    return failure("photo not found: " + std::to_string(id));
  }

  auto settings = catalog::EditSettings::fromJson(rec->editSettings);
  if (params.contains("exposure"))    { settings.exposure    = params["exposure"].get<float>(); }
  if (params.contains("temperature")) { settings.temperature = params["temperature"].get<float>(); }
  if (params.contains("contrast"))    { settings.contrast    = params["contrast"].get<float>(); }
  if (params.contains("saturation"))  { settings.saturation  = params["saturation"].get<float>(); }

  repo_.updateEditSettings(id, settings.toJson());
  repo_.updateThumb(id, "", 0, 0, 0);
  if (adjustedCb_) { adjustedCb_(id); }
  return success();
}

}  // namespace command
