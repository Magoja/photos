#include "ImageRevertHandler.h"

namespace command {

ValidationResult ImageRevertHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("id") || !params["id"].is_number_integer()) {
    return invalid("missing required integer field 'id'");
  }
  return valid();
}

CommandResult ImageRevertHandler::execute(nlohmann::json params) {
  const int64_t id = params["id"].get<int64_t>();
  if (!repo_.findById(id)) {
    return failure("photo not found: " + std::to_string(id));
  }
  repo_.updateEditSettings(id, "{}");
  repo_.updateThumb(id, "", 0, 0, 0);
  if (adjustedCb_) { adjustedCb_(id); }
  return success();
}

}  // namespace command
