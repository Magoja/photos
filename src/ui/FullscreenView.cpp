#include "FullscreenView.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace ui {

FullscreenView::FullscreenView(catalog::PhotoRepository& repo, TextureManager& texMgr)
    : repo_(repo), texMgr_(texMgr) {}

void FullscreenView::setPhotoList(std::vector<int64_t> ids, int64_t currentId) {
    photoIds_   = std::move(ids);
    currentId_  = currentId;
    currentIdx_ = 0;
    for (int i = 0; i < (int)photoIds_.size(); ++i)
        if (photoIds_[i] == currentId) { currentIdx_ = i; break; }
}

void FullscreenView::open(int64_t photoId) {
    currentId_ = photoId;
    open_      = true;
    resetView();
}

void FullscreenView::close() { open_ = false; }

void FullscreenView::resetView() { zoom_ = 1.f; panX_ = 0.f; panY_ = 0.f; }

void FullscreenView::navigate(int delta) {
    if (photoIds_.empty()) return;
    currentIdx_ = std::clamp(currentIdx_ + delta, 0, (int)photoIds_.size() - 1);
    currentId_  = photoIds_[currentIdx_];
    resetView();
}

void FullscreenView::render() {
    if (!open_) return;

    ImGuiIO& io    = ImGui::GetIO();
    ImVec2   scrSz = io.DisplaySize;

    // Fullscreen overlay
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(scrSz);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("##fullscreen", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Keyboard
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || true) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_G)) {
            close();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
            ImGui::IsKeyPressed(ImGuiKey_DownArrow))  navigate(+1);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)  ||
            ImGui::IsKeyPressed(ImGuiKey_UpArrow))    navigate(-1);

        // Backtick → toggle picked
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent) && currentId_ > 0) {
            auto rec = repo_.findById(currentId_);
            if (rec) {
                int newPicked = rec->picked ? 0 : 1;
                repo_.updatePicked(currentId_, newPicked);
                if (pickChangedCb_) pickChangedCb_(currentId_, newPicked);
            }
        }
    }

    // Scroll wheel zoom
    if (io.MouseWheel != 0.f) {
        zoom_ *= (1.f + io.MouseWheel * 0.1f);
        zoom_ = std::clamp(zoom_, 0.1f, 20.f);
    }

    // Pan with mouse drag
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        panX_ += io.MouseDelta.x;
        panY_ += io.MouseDelta.y;
    }

    // Draw texture
    void* tex = texMgr_.get(currentId_);
    float imgW = scrSz.x * zoom_;
    float imgH = scrSz.y * zoom_;
    float x = (scrSz.x - imgW) * 0.5f + panX_;
    float y = (scrSz.y - imgH) * 0.5f + panY_;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddImage(reinterpret_cast<ImTextureID>(tex),
                 {x, y}, {x + imgW, y + imgH});

    // Status overlay
    auto rec = repo_.findById(currentId_);
    if (rec) {
        char info[256];
        std::snprintf(info, sizeof(info),
                      "%s  |  %dx%d  |  %s  |  %s  [%d/%d]",
                      rec->filename.c_str(),
                      rec->widthPx, rec->heightPx,
                      rec->captureTime.c_str(),
                      rec->picked ? "★ Picked" : "",
                      currentIdx_ + 1, (int)photoIds_.size());
        ImGui::SetCursorPos({10, scrSz.y - 30});
        ImGui::TextUnformatted(info);
    }

    ImGui::End();
}

} // namespace ui
