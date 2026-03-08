#include "GridView.h"
#include "imgui.h"
#include <cstdio>

namespace ui {

GridView::GridView(catalog::PhotoRepository& repo, TextureManager& texMgr)
    : repo_(repo), texMgr_(texMgr) {}

void GridView::loadFolder(int64_t folderId, FilterMode filter) {
    folderId_ = folderId;
    filter_   = filter;
    reload();
}

void GridView::reload() {
    bool pickedOnly = (filter_ == FilterMode::Picked);
    if (folderId_ == 0)
        photoIds_ = repo_.queryAll(pickedOnly);
    else
        photoIds_ = repo_.queryByFolder(folderId_, pickedOnly);
}

void GridView::onThumbReady(int64_t photoId, const std::vector<uint8_t>& jpegBytes) {
    texMgr_.upload(photoId, jpegBytes);
}

void GridView::render() {
    float panelW = ImGui::GetContentRegionAvail().x;
    float cellSz = kThumbSize + kThumbPad * 2.f;
    int   cols   = std::max(1, static_cast<int>(panelW / cellSz));

    // Virtual scroll: determine visible row range
    float rowH        = cellSz;
    float scrollY     = ImGui::GetScrollY();
    float viewH       = ImGui::GetWindowHeight();
    int   totalRows   = (static_cast<int>(photoIds_.size()) + cols - 1) / cols;
    int   firstRow    = std::max(0, static_cast<int>(scrollY / rowH) - 1);
    int   lastRow     = std::min(totalRows, static_cast<int>((scrollY + viewH) / rowH) + 2);

    // Skip rows above viewport — submit a Dummy so ImGui tracks the content height
    if (firstRow > 0) {
        ImGui::Dummy({panelW, firstRow * rowH});
    }

    for (int row = firstRow; row < lastRow; ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= (int)photoIds_.size()) break;

            int64_t pid = photoIds_[idx];
            void*   tex = texMgr_.get(pid);
            bool    sel = (pid == selectedId_);

            if (col > 0) ImGui::SameLine();

            ImGui::PushID(static_cast<int>(idx));
            ImVec4 tint = sel ? ImVec4{1.f, 0.8f, 0.2f, 1.f} : ImVec4{1,1,1,1};
            if (ImGui::ImageButton("##img",
                    reinterpret_cast<ImTextureID>(tex),
                    {kThumbSize, kThumbSize}, {0,0}, {1,1},
                    sel ? ImVec4{1.f,0.8f,0.2f,0.4f} : ImVec4{0,0,0,0},
                    tint)) {
                selectedId_ = pid;
                if (onSelectCb_) onSelectCb_(pid);
            }
            // Double-click → open fullscreen
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                selectedId_ = pid;
                if (onSelectCb_) onSelectCb_(pid);
            }
            ImGui::PopID();
        }
    }

    // Reserve space for rows below viewport — Dummy commits the content height
    int remainRows = totalRows - lastRow;
    if (remainRows > 0) {
        ImGui::Dummy({panelW, remainRows * rowH});
    }
}

} // namespace ui
