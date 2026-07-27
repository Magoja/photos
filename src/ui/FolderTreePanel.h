#pragma once
#include "catalog/PhotoRepository.h"
#include <vector>
#include <map>
#include <unordered_set>
#include <optional>
#include <cstdint>
#include <functional>

namespace ui {

class FolderTreePanel {
 public:
  // Fired whenever the folder selection changes; carries the full selected set
  // (a single {0} means "All Photos").
  using SelectionCb = std::function<void(std::vector<int64_t> folderIds)>;
  using DeleteCb = std::function<void(int64_t folderId, const std::string& name)>;

  explicit FolderTreePanel(catalog::PhotoRepository& repo);

  void setOnSelectionChanged(SelectionCb cb) { onSelectionChanged_ = std::move(cb); }
  void setOnDelete(DeleteCb cb) { onDelete_ = std::move(cb); }

  // Replace the selection with a single folder (0 = all).
  void setSelectedFolder(int64_t id);
  // Replace the selection with an explicit set (empty → all).
  void setSelectedFolders(std::vector<int64_t> ids);

  // Refresh folder/volume list from DB
  void refresh();

  // Render the panel; resolves clicks and fires onSelectionChanged when the
  // selection changes (single click = replace, Shift = range, Cmd = toggle).
  void render();

  // Anchor / "current" folder (back-compat single-value accessor).
  int64_t selectedFolder() const { return anchorFolder_; }
  // Full selection as a sorted vector ({0} means all photos).
  std::vector<int64_t> selectedFolders() const;

 private:
  struct PendingClick {
    int64_t id = 0;
    bool shift = false;
    bool ctrl = false;
  };

  void renderFolderChildren(int64_t parentId,
                            const std::map<int64_t, std::vector<catalog::FolderRecord>>& byParent,
                            const std::map<int64_t, int64_t>& counts);
  void renderFolderContextMenu(const catalog::FolderRecord& folder);
  void renderFolderNode(const catalog::FolderRecord& folder, bool hasChildren, int64_t count,
                        const std::map<int64_t, std::vector<catalog::FolderRecord>>& byParent,
                        const std::map<int64_t, int64_t>& counts);
  void resolvePendingClick();
  void selectRange(int64_t fromId, int64_t toId);

  catalog::PhotoRepository& repo_;
  SelectionCb onSelectionChanged_;
  DeleteCb onDelete_;

  std::vector<catalog::VolumeRecord> volumes_;
  std::vector<catalog::FolderRecord> folders_;
  std::map<int64_t, int64_t> counts_;  // folderId -> photo count
  int64_t totalCount_ = 0;

  int64_t anchorFolder_ = 0;                  // anchor for range / "current" folder
  std::unordered_set<int64_t> selectedFolders_{0};  // selected ids ({0} = all)
  std::vector<int64_t> visibleFolderOrder_;   // draw order of visible rows (for range)
  std::optional<PendingClick> pendingClick_;  // click captured this frame, resolved after render
};

}  // namespace ui
