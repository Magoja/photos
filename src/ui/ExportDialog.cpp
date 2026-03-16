#include "ExportDialog.h"
#include "imgui.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace ui {

ExportDialog::ExportDialog(catalog::PhotoRepository& repo) : repo_(repo) {}

void ExportDialog::open(int64_t primaryId, std::vector<int64_t> ids) {
  primaryId_ = primaryId;
  selectedIds_ = std::move(ids);
  open_ = true;
  exporting_ = false;
  finished_ = false;
  doneCount_ = 0;
  totalCount_ = static_cast<int>(selectedIds_.size());
  exportedCount_ = 0;
  errorCount_ = 0;
  exporter_.reset();

  // Default target to Desktop if not set
  if (targetPath_.empty()) {
    targetPath_ = util::desktopDir();
  }
}

void ExportDialog::close() {
  if (exporter_) {
    exporter_->cancel();
  }
  open_ = false;
}

void ExportDialog::startExport() {
  export_ns::ExportPreset gp;
  gp.name = "Google Photos";
  gp.quality = 90;
  gp.maxWidth = 0;   // full-res
  gp.maxHeight = 0;
  gp.targetPath = targetPath_;

  exporter_ = std::make_unique<export_ns::Exporter>(repo_, gp);
  exporter_->setProgressCallback([this](int done, int total) {
    doneCount_ = done;
    totalCount_ = total;
  });
  exporter_->setDoneCallback([this](int exp, int err) {
    exportedCount_ = exp;
    errorCount_ = err;
    finished_ = true;
    exporting_ = false;
  });
  exporting_ = true;
  exporter_->start(selectedIds_);
}

void ExportDialog::render() {
  if (!open_) {
    return;
  }

  ImGui::SetNextWindowSize({480, 240}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
                          {0.5f, 0.5f});
  if (!ImGui::Begin("Export for Google Photos##dlg", &open_)) {
    ImGui::End();
    return;
  }

  ImGui::Text("%d photo(s) selected", static_cast<int>(selectedIds_.size()));
  ImGui::TextDisabled("Google Photos  |  Full resolution  |  Quality 90");
  ImGui::Separator();

  if (!exporting_ && !finished_) {
    // Target folder picker
    ImGui::Text("Target folder:");
    char buf[512] = {};
    std::snprintf(buf, sizeof(buf), "%s", targetPath_.c_str());
    ImGui::SetNextItemWidth(320.f);
    if (ImGui::InputText("##target", buf, sizeof(buf))) {
      targetPath_ = buf;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
      if (auto p = util::pickFolder()) {
        targetPath_ = *p;
      }
    }

    ImGui::Separator();
    const bool canExport = !targetPath_.empty() && !selectedIds_.empty();
    if (!canExport) {
      ImGui::BeginDisabled();
    }
    const std::string btnLabel = "Export " + std::to_string(selectedIds_.size()) + " photos";
    if (ImGui::Button(btnLabel.c_str(), {220.f, 0.f})) {
      std::error_code ec;
      fs::create_directories(targetPath_, ec);
      if (!fs::is_directory(targetPath_)) {
        spdlog::warn("ExportDialog: cannot create target dir '{}': {}", targetPath_,
                     ec ? ec.message() : "not a directory");
      } else {
        startExport();
      }
    }
    if (!canExport) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##pre", {80.f, 0.f})) {
      close();
    }

  } else if (exporting_) {
    const float prog = totalCount_ > 0 ? static_cast<float>(doneCount_) / totalCount_ : 0.f;
    ImGui::ProgressBar(prog, {-1, 0});
    ImGui::Text("%d / %d", doneCount_, totalCount_);
    if (ImGui::Button("Cancel##exp")) {
      exporter_->cancel();
    }

  } else {
    ImGui::TextColored({0.2f, 1.f, 0.2f, 1.f}, "Export complete! %d exported, %d errors",
                       exportedCount_, errorCount_);
    ImGui::Text("Files saved to: %s", targetPath_.c_str());
    if (ImGui::Button("Close")) {
      close();
    }
  }

  ImGui::End();
}

}  // namespace ui
