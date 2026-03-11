#include "GridView.h"
#include "imgui.h"
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
    photoIds_ = (folderId_ == 0)
                ? repo_.queryAll(pickedOnly)
                : repo_.queryByFolder(folderId_, pickedOnly);
    requested_.clear();
}

void GridView::onThumbReady(int64_t photoId, const std::vector<uint8_t>& jpegBytes) {
    texMgr_.upload(photoId, jpegBytes);
}

// ── Layout helpers ────────────────────────────────────────────────────────────

static std::pair<int,int> computeVisibleRowRange(int totalRows,
                                                  float rowH,
                                                  float scrollY,
                                                  float viewH)
{
    int first = std::max(0, static_cast<int>(scrollY / rowH) - 1);
    int last  = std::min(totalRows, static_cast<int>((scrollY + viewH) / rowH) + 2);
    return {first, last};
}

static std::pair<float,float> computeLetterboxSize(int tw, int th,
                                                    float thumbW, float thumbH)
{
    if (tw <= 0 || th <= 0) return {thumbW, thumbH};
    float aspect     = (float)tw / (float)th;
    float cellAspect = thumbW / thumbH;
    if (aspect > cellAspect) return { thumbW, thumbW / aspect };
    return { thumbH * aspect, thumbH };
}

// ── render ────────────────────────────────────────────────────────────────────

void GridView::render() {
    float thumbW = kThumbBase * thumbScale_;
    float thumbH = thumbW * (4.f / 6.f);
    float cellW  = thumbW + kThumbPad * 2.f;
    float cellH  = thumbH + kThumbPad * 2.f;

    float panelW = ImGui::GetContentRegionAvail().x;
    int   cols   = std::max(1, static_cast<int>(panelW / cellW));
    int   totalRows = (static_cast<int>(photoIds_.size()) + cols - 1) / cols;

    auto [firstRow, lastRow] = computeVisibleRowRange(
        totalRows, cellH, ImGui::GetScrollY(), ImGui::GetWindowHeight());

    if (firstRow > 0)
        ImGui::Dummy({panelW, firstRow * cellH});

    for (int row = firstRow; row < lastRow; ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= (int)photoIds_.size()) break;

            int64_t pid = photoIds_[idx];
            void*   tex = texMgr_.get(pid);

            if (tex == texMgr_.placeholder() && thumbMissCb_ && !requested_.count(pid)) {
                requested_.insert(pid);
                thumbMissCb_(pid, repo_.getThumbPath(pid));
            }

            bool sel = (pid == selectedId_);
            auto [tw, th] = texMgr_.getSize(pid);
            auto [imgW, imgH] = computeLetterboxSize(tw, th, thumbW, thumbH);

            if (col > 0) ImGui::SameLine();
            ImVec2 cellPos = ImGui::GetCursorScreenPos();

            ImGui::PushID(idx);
            bool clicked = ImGui::InvisibleButton("##cell", {cellW, cellH});

            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImU32 bgCol = sel ? IM_COL32(80, 60, 10, 255) : IM_COL32(35, 35, 35, 255);
            dl->AddRectFilled(cellPos, {cellPos.x + cellW, cellPos.y + cellH}, bgCol);

            float offX = (thumbW - imgW) * 0.5f + kThumbPad;
            float offY = (thumbH - imgH) * 0.5f + kThumbPad;
            ImVec2 imgMin = {cellPos.x + offX, cellPos.y + offY};
            ImVec2 imgMax = {imgMin.x + imgW,  imgMin.y + imgH};
            dl->AddImage(reinterpret_cast<ImTextureID>(tex), imgMin, imgMax);

            if (sel)
                dl->AddRect(cellPos, {cellPos.x + cellW, cellPos.y + cellH},
                            IM_COL32(255, 200, 50, 255), 0.f, 0, 2.f);

            if (!repo_.libraryRootExists()) {
                ImVec2 badgePos = {cellPos.x + cellW - 18.f, cellPos.y + 2.f};
                dl->AddRectFilled(badgePos, {badgePos.x + 16.f, badgePos.y + 16.f},
                                  IM_COL32(180, 40, 40, 220), 2.f);
                dl->AddText(badgePos, IM_COL32(255, 255, 255, 255), "?");
            }

            if (clicked) {
                selectedId_ = pid;
                if (onSelectCb_) onSelectCb_(pid);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                selectedId_ = pid;
                if (onSelectCb_) onSelectCb_(pid);
            }

            ImGui::PopID();
        }
    }

    int remainRows = totalRows - lastRow;
    if (remainRows > 0)
        ImGui::Dummy({panelW, remainRows * cellH});
}

} // namespace ui
