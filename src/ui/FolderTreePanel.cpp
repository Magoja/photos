#include "FolderTreePanel.h"
#include "imgui.h"
#include <algorithm>

namespace ui {

FolderTreePanel::FolderTreePanel(catalog::PhotoRepository& repo) : repo_(repo) {}

void FolderTreePanel::refresh() {
    nodes_.clear();
    auto folders = repo_.listFolders();
    for (auto& f : folders) {
        Node n;
        n.folder = f;
        n.count  = repo_.folderPhotoCount(f.id);
        nodes_.push_back(n);
    }
}

void FolderTreePanel::render() {
    // "All Photos" entry
    bool allSel = (selectedFolder_ == 0);
    if (allSel) ImGui::PushStyleColor(ImGuiCol_Text, {1.f, 0.8f, 0.2f, 1.f});
    if (ImGui::Selectable("All Photos", allSel)) {
        selectedFolder_ = 0;
        if (onSelect_) onSelect_(0);
    }
    if (allSel) ImGui::PopStyleColor();

    ImGui::Separator();

    for (auto& n : nodes_) {
        bool sel = (selectedFolder_ == n.folder.id);
        char label[256];
        std::snprintf(label, sizeof(label), "%s  (%lld)",
                      n.folder.name.c_str(), (long long)n.count);
        if (ImGui::Selectable(label, sel)) {
            selectedFolder_ = n.folder.id;
            if (onSelect_) onSelect_(n.folder.id);
        }
    }
}

} // namespace ui
