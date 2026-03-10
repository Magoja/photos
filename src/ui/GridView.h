#pragma once
#include "TextureManager.h"
#include "FilterBar.h"
#include "catalog/PhotoRepository.h"
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <string>

namespace ui {

class GridView {
public:
    using SelectCb    = std::function<void(int64_t photoId)>;
    using PickCb      = std::function<void(int64_t photoId, int picked)>;
    using ThumbMissCb = std::function<void(int64_t photoId, std::string thumbPath)>;

    GridView(catalog::PhotoRepository& repo, TextureManager& texMgr);

    void setOnSelect(SelectCb cb)          { onSelectCb_  = std::move(cb); }
    void setOnPick(PickCb cb)              { onPickCb_     = std::move(cb); }
    void setThumbMissCallback(ThumbMissCb cb) { thumbMissCb_ = std::move(cb); }

    // Reload photo IDs for given folder (0 = all)
    void loadFolder(int64_t folderId, FilterMode filter);
    void reload();

    // Render the grid inside the current ImGui window
    void render();

    int64_t selectedId() const { return selectedId_; }
    size_t  photoCount() const { return photoIds_.size(); }

    // Async: call from main thread when a texture decode completes
    void onThumbReady(int64_t photoId, const std::vector<uint8_t>& jpegBytes);

private:
    catalog::PhotoRepository& repo_;
    TextureManager&           texMgr_;
    SelectCb                  onSelectCb_;
    PickCb                    onPickCb_;
    ThumbMissCb               thumbMissCb_;

    std::vector<int64_t>       photoIds_;
    std::unordered_set<int64_t> requested_; // IDs for which thumb load was requested
    int64_t               folderId_   = 0;
    FilterMode            filter_     = FilterMode::All;
    int64_t               selectedId_ = 0;

    static constexpr float kThumbBase   = 120.f;
    static constexpr float kThumbPad    = 4.f;

    float thumbScale_ = 1.f;

public:
    float thumbScale() const     { return thumbScale_; }
    void  setThumbScale(float s) { thumbScale_ = s; }
};

} // namespace ui
