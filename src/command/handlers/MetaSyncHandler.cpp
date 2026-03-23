#include "MetaSyncHandler.h"
#include "catalog/EditSettings.h"

namespace command {
namespace {

catalog::EditSettings mergeSettings(const catalog::EditSettings& src,
                                    const catalog::EditSettings& dst,
                                    const bool applyAdjust,
                                    const bool applyCrop) {
  catalog::EditSettings merged = dst;
  if (applyAdjust) {
    merged.exposure    = src.exposure;
    merged.temperature = src.temperature;
    merged.contrast    = src.contrast;
    merged.saturation  = src.saturation;
  }
  if (applyCrop) {
    merged.crop = src.crop;
  }
  return merged;
}

}  // namespace

ValidationResult MetaSyncHandler::validate(const nlohmann::json& params) const {
  if (!params.contains("primaryId") || !params["primaryId"].is_number_integer()) {
    return invalid("missing required integer field 'primaryId'");
  }
  if (!params.contains("targetIds") || !params["targetIds"].is_array()) {
    return invalid("missing required array field 'targetIds'");
  }
  return valid();
}

CommandResult MetaSyncHandler::execute(nlohmann::json params) {
  const int64_t primaryId  = params["primaryId"].get<int64_t>();
  const bool    syncAdjust = params.value("syncAdjust", false);
  const bool    syncCrop   = params.value("syncCrop",   false);

  const auto srcRec = repo_.findById(primaryId);
  if (!srcRec) {
    return failure("primary photo not found: " + std::to_string(primaryId));
  }
  const auto srcSettings = catalog::EditSettings::fromJson(srcRec->editSettings);

  // Pre-load merged settings for all targets before opening the write transaction.
  // SQLite refuses to COMMIT when a SELECT statement is still active.
  struct TargetUpdate { int64_t id; std::string json; };
  std::vector<TargetUpdate> updates;
  for (const auto& idJson : params["targetIds"]) {
    const int64_t id = idJson.get<int64_t>();
    if (id == primaryId) { continue; }
    const auto tgt = repo_.findById(id);
    if (!tgt) { continue; }
    const auto merged = mergeSettings(
        srcSettings, catalog::EditSettings::fromJson(tgt->editSettings),
        syncAdjust, syncCrop);
    updates.push_back({id, merged.toJson()});
  }

  // Single DB transaction for all writes.
  auto txn = repo_.db().transaction();
  for (const auto& u : updates) {
    repo_.updateEditSettings(u.id, u.json);
  }
  txn.commit();

  if (doneCb_) { doneCb_(); }
  return success();
}

}  // namespace command
