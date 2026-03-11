#include "FullscreenView.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

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
    ImGui::SetNextWindowFocus();   // bring to front of Z stack every frame
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
                toastText_     = newPicked ? "Picked" : "Unpicked";
                toastVisible_  = true;
                toastTimeLeft_ = 1.2f;
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

    // Toast notification
    if (toastVisible_) {
        toastTimeLeft_ -= io.DeltaTime;
        if (toastTimeLeft_ <= 0.f) {
            toastVisible_ = false;
        } else {
            // Fade: alpha goes 1→0 over last 0.4s
            float alpha = std::min(1.f, toastTimeLeft_ / 0.4f);
            ImVec4 col  = {1.f, 1.f, 1.f, alpha};

            ImVec2 textSz = ImGui::CalcTextSize(toastText_.c_str());
            float bw = textSz.x + 40.f, bh = textSz.y + 20.f;
            float bx = (scrSz.x - bw) * 0.5f;
            float by = scrSz.y * 0.35f - bh * 0.5f;

            ImDrawList* tdl = ImGui::GetWindowDrawList();
            tdl->AddRectFilled({bx, by}, {bx + bw, by + bh},
                               ImGui::ColorConvertFloat4ToU32({0.f, 0.f, 0.f, alpha * 0.6f}), 8.f);
            ImGui::SetCursorPos({bx + 20.f, by + 10.f});
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(toastText_.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::End();
}

} // namespace ui
