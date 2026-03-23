#include "ExportHandler.h"

namespace command {

ValidationResult ExportHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("ids") || !params["ids"].is_array()) {
    return invalid("missing required array field 'ids'");
  }
  if (!params.contains("targetPath") || !params["targetPath"].is_string()) {
    return invalid("missing required string field 'targetPath'");
  }
  return valid();
}

CommandResult ExportHandler::execute(nlohmann::json params) {
  if (session_.isRunning()) {
    return failure("export already in progress");
  }

  const std::string targetPath = params["targetPath"].get<std::string>();
  const int         quality    = params.value("quality", 90);

  std::vector<int64_t> ids;
  ids.reserve(params["ids"].size());
  for (const auto& idJson : params["ids"]) {
    ids.push_back(idJson.get<int64_t>());
  }

  session_.start(std::move(ids), targetPath, quality);
  return success();
}

}  // namespace command
