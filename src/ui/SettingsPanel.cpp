#include "SettingsPanel.h"
#include "imgui.h"
#include "util/Platform.h"

namespace ui {

SettingsPanel::SettingsPanel(catalog::PhotoRepository& repo, const std::string& dbPath)
  : repo_(repo), dbPath_(dbPath) {}

void SettingsPanel::open() {
  open_ = true;
}

void SettingsPanel::render() {
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
  }
  ImGui::End();
}

}  // namespace ui
