#include "DeleteConfirmDialog.h"
#include "imgui.h"

namespace ui {

void DeleteConfirmDialog::openForPhotos(std::vector<int64_t> ids) {
  const size_t count = ids.size();
  target_ =
    Target{.kind = Kind::Photos,
           .ids = std::move(ids),
           .folderId = 0,
           .message = "Delete " + std::to_string(count) + (count == 1 ? " photo" : " photos") +
                      " from the catalog?\nThumbnails will be removed. This cannot be undone."};
  open_ = true;
}

void DeleteConfirmDialog::openForFolder(int64_t folderId, const std::string& name) {
  target_ = Target{.kind = Kind::Folder,
                   .ids = {},
                   .folderId = folderId,
                   .message = "Delete folder \"" + name +
                              "\" and all its photos from the catalog?\n"
                              "Thumbnails will be removed. This cannot be undone."};
  open_ = true;
}

void DeleteConfirmDialog::close() {
  open_ = false;
}

namespace {

bool confirmButton() {
  ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 50, 50, 255));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(210, 70, 70, 255));
  const bool clicked = ImGui::Button("Delete");
  ImGui::PopStyleColor(2);
  return clicked;
}

}  // namespace

void DeleteConfirmDialog::render() {
  if (!open_) {
    return;
  }

  ImGui::OpenPopup("Delete?##confirm");
  if (!ImGui::BeginPopupModal("Delete?##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  ImGui::TextUnformatted(target_.message.c_str());
  ImGui::Separator();

  if (confirmButton()) {
    if (onConfirm_) {
      onConfirm_(target_);
    }
    ImGui::CloseCurrentPopup();
    close();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    ImGui::CloseCurrentPopup();
    close();
  }

  ImGui::EndPopup();
}

}  // namespace ui
