#include "CatalogPickHandler.h"

namespace command {

ValidationResult CatalogPickHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("id") || !params["id"].is_number_integer()) {
    return invalid("missing required integer field 'id'");
  }
  if (!params.contains("picked") || !params["picked"].is_number_integer()) {
    return invalid("missing required integer field 'picked'");
  }
  return valid();
}

CommandResult CatalogPickHandler::execute(nlohmann::json params) {
  const int64_t id     = params["id"].get<int64_t>();
  const int     picked = params["picked"].get<int>();

  const auto rec = repo_.findById(id);
  if (!rec) {
    return failure("photo not found: " + std::to_string(id));
  }

  repo_.updatePicked(id, picked);
  if (pickedCb_) { pickedCb_(id, picked); }
  return success();
}

}  // namespace command
