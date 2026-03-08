#include "SettingsPanel.h"
#include "imgui.h"
#include "util/Platform.h"
#include <cstring>

namespace ui {

SettingsPanel::SettingsPanel(catalog::PhotoRepository& repo, const std::string& dbPath)
    : repo_(repo), dbPath_(dbPath) {}

void SettingsPanel::open() {
    std::string lr = repo_.getSetting("library_root");
    std::string tc = repo_.getSetting("thumb_cache_dir");
    std::strncpy(libraryRoot_,   lr.c_str(), sizeof(libraryRoot_) - 1);
    std::strncpy(thumbCacheDir_, tc.c_str(), sizeof(thumbCacheDir_) - 1);
    open_ = true;
}

void SettingsPanel::render() {
    if (!open_) return;

    ImGui::SetNextWindowSize({520, 220}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &open_)) {
        ImGui::Text("Library Root Folder:");
        ImGui::SetNextItemWidth(340);
        ImGui::InputText("##libroot", libraryRoot_, sizeof(libraryRoot_));
        ImGui::SameLine();
        if (ImGui::Button("Browse##lr")) {
            if (auto p = util::pickFolder()) {
                std::strncpy(libraryRoot_, p->c_str(), sizeof(libraryRoot_) - 1);
            }
        }

        ImGui::Spacing();
        ImGui::Text("Thumbnail Cache Path:");
        ImGui::SetNextItemWidth(340);
        ImGui::InputText("##thumbdir", thumbCacheDir_, sizeof(thumbCacheDir_));
        ImGui::SameLine();
        if (ImGui::Button("Browse##tc")) {
            if (auto p = util::pickFolder()) {
                std::strncpy(thumbCacheDir_, p->c_str(), sizeof(thumbCacheDir_) - 1);
            }
        }

        ImGui::Spacing();
        ImGui::Text("Database Path:");
        ImGui::TextDisabled("%s", dbPath_.c_str());
        ImGui::TextDisabled("(Changing DB path requires app restart)");

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Save")) {
            repo_.setSetting("library_root",    libraryRoot_);
            repo_.setSetting("thumb_cache_dir", thumbCacheDir_);
            open_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            open_ = false;
        }
    }
    ImGui::End();
}

} // namespace ui
