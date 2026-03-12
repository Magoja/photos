#include "FolderTreePanel.h"
#include "imgui.h"
#include <map>
#include <ranges>
#include <algorithm>
#include <span>
#include <cstdio>

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

// ── Tree rendering helper ─────────────────────────────────────────────────────

static std::map<int64_t, std::vector<catalog::FolderRecord>> groupByParent(
  std::span<const catalog::FolderRecord> folders) {
  std::map<int64_t, std::vector<catalog::FolderRecord>> byParent;
  for (auto& f : folders) {
    byParent[f.parentId].push_back(f);
  }
  return byParent;
}

void FolderTreePanel::renderFolderChildren(
  int64_t parentId, const std::map<int64_t, std::vector<catalog::FolderRecord>>& byParent,
  const std::map<int64_t, int64_t>& counts) {
  auto it = byParent.find(parentId);
  if (it == byParent.end()) {
    return;
  }

  for (auto& f : it->second) {
    bool hasChildren = byParent.count(f.id) > 0;
    int64_t cnt = 0;
    if (auto ci = counts.find(f.id); ci != counts.end()) {
      cnt = ci->second;
    }

    char label[512];
    std::snprintf(label, sizeof(label), "%s  (%lld)##f%lld", f.name.c_str(), (long long)cnt,
                  (long long)f.id);

    if (hasChildren) {
      bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
      if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selectedFolder_ = f.id;
        if (onSelect_) {
          onSelect_(f.id);
        }
      }
      if (open) {
        renderFolderChildren(f.id, byParent, counts);
        ImGui::TreePop();
      }
    } else {
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
      if (selectedFolder_ == f.id) {
        flags |= ImGuiTreeNodeFlags_Selected;
      }
      ImGui::TreeNodeEx(label, flags);
      if (ImGui::IsItemClicked()) {
        selectedFolder_ = f.id;
        if (onSelect_) {
          onSelect_(f.id);
        }
      }
      ImGui::TreePop();
    }
  }
}

// ── render ────────────────────────────────────────────────────────────────────

void FolderTreePanel::render() {
  char allLabel[64];
  std::snprintf(allLabel, sizeof(allLabel), "All Photos  (%lld)", (long long)totalCount_);
  ImGuiTreeNodeFlags allFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (selectedFolder_ == 0) {
    allFlags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::TreeNodeEx(allLabel, allFlags);
  if (ImGui::IsItemClicked()) {
    selectedFolder_ = 0;
    if (onSelect_) {
      onSelect_(0);
    }
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
}

}  // namespace ui
