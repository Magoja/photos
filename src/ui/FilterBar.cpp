#include "FilterBar.h"
#include "imgui.h"

namespace ui {

bool FilterBar::render() {
    changed_ = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4, 2});

    bool allSelected    = (mode_ == FilterMode::All);
    bool pickedSelected = (mode_ == FilterMode::Picked);

    if (allSelected)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("All")) {
        if (mode_ != FilterMode::All) { mode_ = FilterMode::All; changed_ = true; }
    }
    if (allSelected) ImGui::PopStyleColor();

    ImGui::SameLine();

    if (pickedSelected)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("Picked")) {
        if (mode_ != FilterMode::Picked) { mode_ = FilterMode::Picked; changed_ = true; }
    }
    if (pickedSelected) ImGui::PopStyleColor();

    ImGui::PopStyleVar();
    return changed_;
}

} // namespace ui
