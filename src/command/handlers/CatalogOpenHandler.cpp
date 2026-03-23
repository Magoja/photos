#include "CatalogOpenHandler.h"

namespace command {

ValidationResult CatalogOpenHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("id") || !params["id"].is_number_integer()) {
    return invalid("missing required integer field 'id'");
  }
  return valid();
}

CommandResult CatalogOpenHandler::execute(nlohmann::json params) {
  const int64_t id = params["id"].get<int64_t>();
  if (selectCb_) { selectCb_(id); }
  return success();
}

}  // namespace command
