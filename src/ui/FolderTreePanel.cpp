#include "FolderTreePanel.h"
#include "imgui.h"
#include <map>
#include <ranges>
#include <algorithm>
#include <span>
#include <cstdio>
#include <string>

namespace ui {

FolderTreePanel::FolderTreePanel(catalog::PhotoRepository& repo) : repo_(repo) {}

void FolderTreePanel::refresh() {
  volumes_ = repo_.listVolumes();
  folders_ = repo_.listFolders();
  counts_ = repo_.allFolderPhotoCounts();
  totalCount_ = 0;
  for (const auto& [id, c] : counts_) {
    totalCount_ += c;
  }
}

// ── Selection state ─────────────────────────────────────────────────────────

void FolderTreePanel::setSelectedFolder(int64_t id) {
  setSelectedFolders({id});
}

void FolderTreePanel::setSelectedFolders(std::vector<int64_t> ids) {
  if (ids.empty()) {
    ids = {0};
  }
  selectedFolders_ = std::unordered_set<int64_t>(ids.begin(), ids.end());
  anchorFolder_ = ids.front();
}

std::vector<int64_t> FolderTreePanel::selectedFolders() const {
  std::vector<int64_t> out(selectedFolders_.begin(), selectedFolders_.end());
  std::ranges::sort(out);
  return out;
}

// Selects the inclusive range of visible folders between fromId and toId,
// keeping fromId as the anchor. Falls back to a plain single select if either
// endpoint is not currently visible.
void FolderTreePanel::selectRange(int64_t fromId, int64_t toId) {
  const auto itA = std::ranges::find(visibleFolderOrder_, fromId);
  const auto itB = std::ranges::find(visibleFolderOrder_, toId);
  if (itA == visibleFolderOrder_.end() || itB == visibleFolderOrder_.end()) {
    selectedFolders_ = {toId};
    anchorFolder_ = toId;
    return;
  }
  const auto lo = std::min(itA, itB);
  const auto hi = std::max(itA, itB);
  selectedFolders_.clear();
  for (auto it = lo; it <= hi; ++it) {
    selectedFolders_.insert(*it);
  }
}

void FolderTreePanel::resolvePendingClick() {
  if (!pendingClick_) {
    return;
  }
  const PendingClick pc = *pendingClick_;
  pendingClick_.reset();

  const bool currentlyAll = selectedFolders_.count(0) > 0;
  if (pc.id == 0) {
    selectedFolders_ = {0};  // "All Photos" is exclusive
    anchorFolder_ = 0;
  } else if (currentlyAll) {
    selectedFolders_ = {pc.id};  // cannot extend "all" — start a fresh selection
    anchorFolder_ = pc.id;
  } else if (pc.shift && anchorFolder_ != 0) {
    selectRange(anchorFolder_, pc.id);
  } else if (pc.ctrl) {
    if (selectedFolders_.count(pc.id)) {
      selectedFolders_.erase(pc.id);
      if (selectedFolders_.empty()) {
        selectedFolders_ = {0};  // deselecting the last folder falls back to All
        anchorFolder_ = 0;
      } else if (anchorFolder_ == pc.id) {
        anchorFolder_ = *selectedFolders_.begin();
      }
    } else {
      selectedFolders_.insert(pc.id);
      anchorFolder_ = pc.id;
    }
  } else {
    selectedFolders_ = {pc.id};
    anchorFolder_ = pc.id;
  }

  if (onSelectionChanged_) {
    onSelectionChanged_(selectedFolders());
  }
}

// ── Tree rendering helper ─────────────────────────────────────────────────────

static std::map<int64_t, std::vector<catalog::FolderRecord>> groupByParent(
  std::span<const catalog::FolderRecord> folders) {
  std::map<int64_t, std::vector<catalog::FolderRecord>> byParent;
  for (auto& f : folders) {
    byParent[f.parentId].push_back(f);
  }
  return byParent;
}

void FolderTreePanel::renderFolderContextMenu(const catalog::FolderRecord& folder) {
  if (!ImGui::BeginPopupContextItem()) {
    return;
  }
  if (ImGui::MenuItem("Delete Folder") && onDelete_) {
    onDelete_(folder.id, folder.name);
  }
  ImGui::EndPopup();
}

void FolderTreePanel::renderFolderNode(
  const catalog::FolderRecord& f, bool hasChildren, int64_t count,
  const std::map<int64_t, std::vector<catalog::FolderRecord>>& byParent,
  const std::map<int64_t, int64_t>& counts) {
  visibleFolderOrder_.push_back(f.id);

  char label[512];
  std::snprintf(label, sizeof(label), "%s  (%lld)##f%lld", f.name.c_str(), (long long)count,
                (long long)f.id);

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
  if (!hasChildren) {
    flags |= ImGuiTreeNodeFlags_Leaf;
  }
  if (selectedFolders_.count(f.id)) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  const bool open = ImGui::TreeNodeEx(label, flags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    const auto& io = ImGui::GetIO();
    pendingClick_ = PendingClick{.id = f.id, .shift = io.KeyShift, .ctrl = io.KeyCtrl};
  }
  renderFolderContextMenu(f);
  if (open) {
    renderFolderChildren(f.id, byParent, counts);
    ImGui::TreePop();
  }
}

void FolderTreePanel::renderFolderChildren(
  int64_t parentId, const std::map<int64_t, std::vector<catalog::FolderRecord>>& byParent,
  const std::map<int64_t, int64_t>& counts) {
  auto it = byParent.find(parentId);
  if (it == byParent.end()) {
    return;
  }

  for (auto& f : it->second) {
    const bool hasChildren = byParent.count(f.id) > 0;
    int64_t cnt = 0;
    if (auto ci = counts.find(f.id); ci != counts.end()) {
      cnt = ci->second;
    }
    renderFolderNode(f, hasChildren, cnt, byParent, counts);
  }
}

// ── render ────────────────────────────────────────────────────────────────────

void FolderTreePanel::render() {
  visibleFolderOrder_.clear();

  char allLabel[64];
  std::snprintf(allLabel, sizeof(allLabel), "All Photos  (%lld)", (long long)totalCount_);
  ImGuiTreeNodeFlags allFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (selectedFolders_.count(0)) {
    allFlags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::TreeNodeEx(allLabel, allFlags);
  if (ImGui::IsItemClicked()) {
    pendingClick_ = PendingClick{.id = 0, .shift = false, .ctrl = false};
  }

  ImGui::Separator();

  if (volumes_.empty()) {
    auto byParent = groupByParent(folders_);
    renderFolderChildren(0, byParent, counts_);
  } else {
    for (auto& vol : volumes_) {
      auto volFolders = repo_.listFolders(vol.id);
      if (volFolders.empty()) {
        continue;
      }

      auto vByParent = groupByParent(volFolders);

      char volLabel[256];
      std::snprintf(volLabel, sizeof(volLabel), "[Disk] %s##v%lld",
                    vol.label.empty() ? "Untitled" : vol.label.c_str(), (long long)vol.id);
      bool volOpen = ImGui::TreeNodeEx(
        volLabel, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen);
      if (volOpen) {
        renderFolderChildren(0, vByParent, counts_);
        ImGui::TreePop();
      }
    }

    std::vector<catalog::FolderRecord> orphans;
    std::ranges::copy_if(folders_, std::back_inserter(orphans),
                         [](const catalog::FolderRecord& f) { return f.volumeId == 0; });
    if (!orphans.empty()) {
      auto oByParent = groupByParent(orphans);
      if (ImGui::TreeNodeEx("Library##orphans",
                            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
        renderFolderChildren(0, oByParent, counts_);
        ImGui::TreePop();
      }
    }
  }

  resolvePendingClick();
}

}  // namespace ui
