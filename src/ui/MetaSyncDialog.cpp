#include "MetaSyncDialog.h"
#include "ThumbEditApplier.h"
#include "catalog/EditSettings.h"
#include "command/CommandRegistry.h"
#include "imgui.h"
#include <spdlog/spdlog.h>

namespace ui {

MetaSyncDialog::MetaSyncDialog(catalog::PhotoRepository& repo, TextureManager& texMgr)
  : repo_(repo), texMgr_(texMgr) {}

void MetaSyncDialog::open(int64_t primaryId, std::vector<int64_t> targetIds) {
  primaryId_ = primaryId;
  targetIds_.clear();
  for (const int64_t id : targetIds) {
    if (id != primaryId_) {
      targetIds_.push_back(id);
    }
  }

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

  // Pre-load merged settings and thumb path for each target
  struct TargetUpdate {
    int64_t id;
    std::string json;
    catalog::EditSettings merged;
    std::string thumbPath;
  };
  std::vector<TargetUpdate> updates;
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
    updates.push_back({id, merged.toJson(), merged, tgtRec->thumbPath});
  }

  // Write phase: dispatch through registry for logging; fall back to direct writes.
  if (registry_) {
    nlohmann::json tids = nlohmann::json::array();
    for (const int64_t id : targetIds_) { tids.push_back(id); }
    registry_->dispatch("metasync.apply", {
        {"primaryId",  primaryId_},
        {"targetIds",  tids},
        {"syncAdjust", syncAdjust_},
        {"syncCrop",   syncCrop_}
    });
  } else {
    auto txn = repo_.db().transaction();
    for (const auto& u : updates) {
      repo_.updateEditSettings(u.id, u.json);
    }
    txn.commit();
  }

  // Compute edited thumbnails in memory; drain happens at the top of the next
  // frame in main.mm (before grid.render()) to avoid a mid-frame use-after-free.
  for (const auto& u : updates) {
    if (u.thumbPath.empty()) { continue; }
    if (auto res = ui::applyEditsToThumb(u.thumbPath, u.merged)) {
      pendingThumbUpdates_.push_back({u.id, std::move(res->rgba), res->w, res->h});
    }
  }

  spdlog::info("MetaSync: synced settings from photo {} to {} photos", primaryId_,
               targetIds_.size());
}

std::vector<MetaSyncDialog::ThumbUpdate> MetaSyncDialog::takePendingThumbUpdates() {
  return std::exchange(pendingThumbUpdates_, {});
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
