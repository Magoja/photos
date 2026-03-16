#include "MetaSyncDialog.h"
#include "catalog/EditSettings.h"
#include "imgui.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <cmath>
#include <cstring>

namespace ui {

namespace {

// Decode a thumbnail JPEG from disk, apply edit_settings adjustments (same
// formulas as Exporter::applyAdjustments / applyCrop), and return the result
// as RGBA pixels ready for TextureManager::uploadRgba.
static std::optional<MetaSyncDialog::ThumbUpdate>
applyEditsToThumb(int64_t id, const std::string& thumbPath,
                  const catalog::EditSettings& s) {
  std::ifstream f(thumbPath, std::ios::binary);
  if (!f) { return std::nullopt; }
  const std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(f)), {});
  if (jpeg.empty()) { return std::nullopt; }

  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  if (!TextureManager::decodeJpeg(jpeg, rgba, w, h)) { return std::nullopt; }

  const float eMul  = std::pow(2.f, s.exposure);
  const float t     = s.temperature / 100.f;
  const float rMul  = 1.f + t * 0.30f;
  const float gMul  = 1.f + t * 0.05f;
  const float bMul  = 1.f - t * 0.30f;
  const float cFact = 1.f + s.contrast   / 100.f;
  const float sFact = 1.f + s.saturation / 100.f;

  for (int i = 0, n = w * h; i < n; ++i) {
    float r = rgba[i * 4 + 0];
    float g = rgba[i * 4 + 1];
    float b = rgba[i * 4 + 2];

    r *= eMul;  g *= eMul;  b *= eMul;
    r *= rMul;  g *= gMul;  b *= bMul;
    r = 128.f + (r - 128.f) * cFact;
    g = 128.f + (g - 128.f) * cFact;
    b = 128.f + (b - 128.f) * cFact;
    const float L = 0.299f * r + 0.587f * g + 0.114f * b;
    r = L + (r - L) * sFact;
    g = L + (g - L) * sFact;
    b = L + (b - L) * sFact;

    rgba[i * 4 + 0] = static_cast<uint8_t>(std::clamp(r, 0.f, 255.f));
    rgba[i * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0.f, 255.f));
    rgba[i * 4 + 2] = static_cast<uint8_t>(std::clamp(b, 0.f, 255.f));
  }

  // Apply crop
  const int cropX = static_cast<int>(s.crop.x * w);
  const int cropY = static_cast<int>(s.crop.y * h);
  const int cropW = std::max(1, static_cast<int>(s.crop.w * w));
  const int cropH = std::max(1, static_cast<int>(s.crop.h * h));

  if (cropX == 0 && cropY == 0 && cropW == w && cropH == h) {
    return MetaSyncDialog::ThumbUpdate{id, std::move(rgba), w, h};
  }

  std::vector<uint8_t> cropped(static_cast<size_t>(cropW) * cropH * 4);
  for (int row = 0; row < cropH; ++row) {
    const int srcRow = std::clamp(cropY + row, 0, h - 1);
    const int srcCol = std::clamp(cropX, 0, w - 1);
    const int copyW  = std::min(cropW, w - srcCol);
    std::memcpy(cropped.data() + static_cast<size_t>(row) * cropW * 4,
                rgba.data() + (static_cast<size_t>(srcRow) * w + srcCol) * 4,
                static_cast<size_t>(copyW) * 4);
  }
  return MetaSyncDialog::ThumbUpdate{id, std::move(cropped), cropW, cropH};
}

}  // namespace

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

  // Write phase: no reads inside the transaction
  auto txn = repo_.db().transaction();
  for (const auto& u : updates) {
    repo_.updateEditSettings(u.id, u.json);
  }
  txn.commit();

  // Compute edited thumbnails in memory; drain happens at the top of the next
  // frame in main.mm (before grid.render()) to avoid a mid-frame use-after-free.
  for (const auto& u : updates) {
    if (u.thumbPath.empty()) { continue; }
    if (auto res = applyEditsToThumb(u.id, u.thumbPath, u.merged)) {
      pendingThumbUpdates_.push_back(std::move(*res));
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
