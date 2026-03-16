#include "MetaSyncDialog.h"
#include "GridView.h"
#include "catalog/EditSettings.h"
#include "imgui.h"
#include <spdlog/spdlog.h>

namespace ui {

MetaSyncDialog::MetaSyncDialog(catalog::PhotoRepository& repo, TextureManager& texMgr)
  : repo_(repo), texMgr_(texMgr) {}

void MetaSyncDialog::open(int64_t primaryId, std::vector<int64_t> targetIds) {
  primaryId_ = primaryId;
  targetIds_ = std::move(targetIds);

  const auto rec = repo_.findById(primaryId_);
  sourceFilename_ = rec ? rec->filename : "";
  open_ = true;
}

// ── Merge helpers ─────────────────────────────────────────────────────────────

static catalog::EditSettings mergeSettings(const catalog::EditSettings& src,
                                           const catalog::EditSettings& dst,
                                           bool applyAdjust, bool applyCrop) {
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

void MetaSyncDialog::performSync() {
  // Load all records before opening the write transaction.
  // SQLite refuses to COMMIT when a SELECT statement is still "active"
  // (stepped to SQLITE_ROW but not yet reset), so we must drain all reads first.
  const auto srcRec = repo_.findById(primaryId_);
  if (!srcRec) {
    return;
  }
  const catalog::EditSettings srcSettings =
    catalog::EditSettings::fromJson(srcRec->editSettings);

  // Pre-load merged JSON for each target (all reads before the write transaction)
  struct IdAndJson { int64_t id; std::string json; };
  std::vector<IdAndJson> updates;
  updates.reserve(targetIds_.size());
  for (const int64_t id : targetIds_) {
    const auto tgtRec = repo_.findById(id);
    if (!tgtRec) {
      continue;
    }
    const catalog::EditSettings tgtSettings =
      catalog::EditSettings::fromJson(tgtRec->editSettings);
    const catalog::EditSettings merged =
      mergeSettings(srcSettings, tgtSettings, syncAdjust_, syncCrop_);
    updates.push_back({id, merged.toJson()});
  }

  // Write phase: no reads inside the transaction
  auto txn = repo_.db().transaction();
  for (const auto& [id, json] : updates) {
    repo_.updateEditSettings(id, json);
  }
  txn.commit();

  // Evict modified photos from texture LRU so the grid re-fetches updated thumbnails
  for (const int64_t id : targetIds_) {
    texMgr_.evict(id);
    texMgr_.evict(id + ui::GridView::kMicroOffset);
  }

  spdlog::info("MetaSync: synced settings from photo {} to {} photos", primaryId_,
               targetIds_.size());
}

// ── render ────────────────────────────────────────────────────────────────────

void MetaSyncDialog::render() {
  if (!open_) {
    return;
  }

  ImGui::SetNextWindowSize({420, 240}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
                          {0.5f, 0.5f});

  if (!ImGui::Begin("Sync Metadata##dlg", &open_)) {
    ImGui::End();
    return;
  }

  // Header: source photo info
  const auto* thumb = texMgr_.get(primaryId_);
  if (thumb && thumb != texMgr_.placeholder()) {
    ImGui::Image(reinterpret_cast<ImTextureID>(thumb), {64.f, 43.f});
    ImGui::SameLine();
  }
  ImGui::BeginGroup();
  ImGui::Text("Source: %s", sourceFilename_.c_str());
  ImGui::TextDisabled("Sync settings to %zu photo(s)", targetIds_.size());
  ImGui::EndGroup();

  ImGui::Separator();

  // Field-group checkboxes
  ImGui::Text("Fields to sync:");
  ImGui::Checkbox("Adjustments  (exposure, temperature, contrast, saturation)", &syncAdjust_);
  ImGui::Checkbox("Crop  (x, y, w, h, angle)", &syncCrop_);

  ImGui::Separator();

  const bool canSync = syncAdjust_ || syncCrop_;
  if (!canSync) {
    ImGui::BeginDisabled();
  }
  const std::string btnLabel = "Sync " + std::to_string(targetIds_.size()) + " photos";
  if (ImGui::Button(btnLabel.c_str(), {200.f, 0.f})) {
    performSync();
    open_ = false;
    if (doneCb_) {
      doneCb_();
    }
  }
  if (!canSync) {
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", {80.f, 0.f})) {
    open_ = false;
  }

  ImGui::End();
}

}  // namespace ui
