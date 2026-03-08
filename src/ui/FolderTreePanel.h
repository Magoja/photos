#pragma once
#include "catalog/PhotoRepository.h"
#include <vector>
#include <map>
#include <cstdint>
#include <functional>

namespace ui {

class FolderTreePanel {
public:
    using SelectCb = std::function<void(int64_t folderId)>;

    explicit FolderTreePanel(catalog::PhotoRepository& repo);

    void setOnSelect(SelectCb cb) { onSelect_ = std::move(cb); }

    // Refresh folder/volume list from DB
    void refresh();

    // Render the panel; calls onSelect when user clicks a folder (0 = all photos)
    void render();

    int64_t selectedFolder() const { return selectedFolder_; }

private:
    void renderFolderChildren(
        int64_t parentId,
        const std::map<int64_t, std::vector<catalog::FolderRecord>>& byParent,
        const std::map<int64_t, int64_t>& counts);

    catalog::PhotoRepository& repo_;
    SelectCb                  onSelect_;

    std::vector<catalog::VolumeRecord>  volumes_;
    std::vector<catalog::FolderRecord>  folders_;
    std::map<int64_t, int64_t>          counts_;     // folderId -> photo count
    int64_t                             totalCount_     = 0;
    int64_t                             selectedFolder_ = 0; // 0 = all
};

} // namespace ui
