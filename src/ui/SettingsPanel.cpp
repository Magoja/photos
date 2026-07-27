#include "SettingsPanel.h"
#include "imgui.h"
#include "util/Platform.h"

namespace ui {

SettingsPanel::SettingsPanel(catalog::PhotoRepository& repo, const std::string& dbPath)
  : repo_(repo), dbPath_(dbPath) {}

void SettingsPanel::open() {
  open_ = true;
}

void SettingsPanel::setReimportResult(int updated, int errors, int total) {
  reimportResult_ = {.updated = updated, .errors = errors, .total = total};
  pendingOpenReimportPopup_ = true;
}

void SettingsPanel::renderReimportResultPopup() {
  static constexpr const char* kPopupId = "Reimport Complete";
  if (pendingOpenReimportPopup_) {
    ImGui::OpenPopup(kPopupId);
    pendingOpenReimportPopup_ = false;
  }
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
  if (ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Reimported metadata from source files.");
    ImGui::Spacing();
    ImGui::Text("Updated:        %d", reimportResult_.updated);
    ImGui::Text("Skipped/errors: %d", reimportResult_.errors);
    ImGui::Text("Total photos:   %d", reimportResult_.total);
    ImGui::Spacing();
    if (ImGui::Button("OK", {120, 0})) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void SettingsPanel::render() {
  // The completion popup must render regardless of whether the Settings window
  // is still open (the reimport may finish after the user closes it).
  renderReimportResultPopup();
  if (!open_) {
    return;
  }

  ImGui::SetNextWindowSize({520, 130}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Settings", &open_)) {
    ImGui::Text("Library Folder:");
    ImGui::TextDisabled("%s",
                        repo_.libraryRoot().empty() ? "(not set)" : repo_.libraryRoot().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Change...")) {
      if (auto p = util::pickFolder()) {
        repo_.setSetting("library_root", *p);
        repo_.setLibraryRoot(*p);
      }
    }

    ImGui::Spacing();
    ImGui::Text("Database Path:");
    ImGui::TextDisabled("%s", dbPath_.c_str());
    ImGui::TextDisabled("(Changing DB path requires app restart)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Clear Preview Cache")) {
      if (clearCacheCb_) {
        clearCacheCb_();
      }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Deletes all cached thumbnails and forces regeneration.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Debug");
    if (ImGui::Button("Reimport Metadata")) {
      if (reimportMetadataCb_) {
        reimportMetadataCb_();
      }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Re-reads EXIF/GPS from every photo's source file into the DB.");
  }
  ImGui::End();
}

}  // namespace ui
