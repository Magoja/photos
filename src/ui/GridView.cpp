#include "GridView.h"
#include "imgui.h"
#include <cstdio>
#include <algorithm>

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
    requested_.clear();
}

void GridView::onThumbReady(int64_t photoId, const std::vector<uint8_t>& jpegBytes) {
    texMgr_.upload(photoId, jpegBytes);
}

void GridView::render() {
    float thumbW = kThumbBase * thumbScale_;
    float thumbH = thumbW * (4.f / 6.f);   // 6:4 aspect cell
    float cellW  = thumbW + kThumbPad * 2.f;
    float cellH  = thumbH + kThumbPad * 2.f;

    float panelW = ImGui::GetContentRegionAvail().x;
    int   cols   = std::max(1, static_cast<int>(panelW / cellW));

    // Virtual scroll: determine visible row range
    float rowH      = cellH;
    float scrollY   = ImGui::GetScrollY();
    float viewH     = ImGui::GetWindowHeight();
    int   totalRows = (static_cast<int>(photoIds_.size()) + cols - 1) / cols;
    int   firstRow  = std::max(0, static_cast<int>(scrollY / rowH) - 1);
    int   lastRow   = std::min(totalRows, static_cast<int>((scrollY + viewH) / rowH) + 2);

    // Skip rows above viewport
    if (firstRow > 0) {
        ImGui::Dummy({panelW, firstRow * rowH});
    }

    for (int row = firstRow; row < lastRow; ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= (int)photoIds_.size()) break;

            int64_t pid = photoIds_[idx];
            void*   tex = texMgr_.get(pid);

            // Request async thumb load on first miss
            if (tex == texMgr_.placeholder() && thumbMissCb_ && !requested_.count(pid)) {
                requested_.insert(pid);
                thumbMissCb_(pid, repo_.getThumbPath(pid));
            }
            bool sel = (pid == selectedId_);

            // Compute letterbox draw rect
            float imgW, imgH;
            auto [tw, th] = texMgr_.getSize(pid);
            if (tw > 0 && th > 0) {
                float aspect     = (float)tw / (float)th;
                float cellAspect = thumbW / thumbH;
                if (aspect > cellAspect) { imgW = thumbW; imgH = thumbW / aspect; }
                else                     { imgH = thumbH; imgW = thumbH * aspect; }
            } else {
                imgW = thumbW; imgH = thumbH;  // placeholder: fill cell
            }

            if (col > 0) ImGui::SameLine();
            ImVec2 cellPos = ImGui::GetCursorScreenPos();

            ImGui::PushID(idx);
            bool clicked = ImGui::InvisibleButton("##cell", {cellW, cellH});

            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Cell background
            ImU32 bgCol = sel ? IM_COL32(80, 60, 10, 255) : IM_COL32(35, 35, 35, 255);
            dl->AddRectFilled(cellPos, {cellPos.x + cellW, cellPos.y + cellH}, bgCol);

            // Image, centered within cell
            float offX = (thumbW - imgW) * 0.5f + kThumbPad;
            float offY = (thumbH - imgH) * 0.5f + kThumbPad;
            ImVec2 imgMin = {cellPos.x + offX, cellPos.y + offY};
            ImVec2 imgMax = {imgMin.x + imgW,  imgMin.y + imgH};
            dl->AddImage(reinterpret_cast<ImTextureID>(tex), imgMin, imgMax);

            // Selection border
            if (sel)
                dl->AddRect(cellPos, {cellPos.x + cellW, cellPos.y + cellH},
                            IM_COL32(255, 200, 50, 255), 0.f, 0, 2.f);

            if (clicked) {
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

    // Reserve space for rows below viewport
    int remainRows = totalRows - lastRow;
    if (remainRows > 0) {
        ImGui::Dummy({panelW, remainRows * rowH});
    }
}

} // namespace ui
