#pragma once
#include "catalog/PhotoRepository.h"
#include <vector>
#include <cstdint>
#include <functional>

namespace ui {

class FolderTreePanel {
public:
    using SelectCb = std::function<void(int64_t folderId)>;

    explicit FolderTreePanel(catalog::PhotoRepository& repo);

    void setOnSelect(SelectCb cb) { onSelect_ = std::move(cb); }

    // Refresh folder list from DB
    void refresh();

    // Render the panel; calls onSelect when user clicks a folder (0 = all photos)
    void render();

    int64_t selectedFolder() const { return selectedFolder_; }

private:
    catalog::PhotoRepository& repo_;
    SelectCb                  onSelect_;

    struct Node {
        catalog::FolderRecord folder;
        int64_t               count = 0;
    };
    std::vector<Node>  nodes_;
    int64_t            selectedFolder_ = 0; // 0 = all
};

} // namespace ui
