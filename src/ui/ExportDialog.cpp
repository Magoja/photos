#include "ExportDialog.h"
#include "command/CommandRegistry.h"
#include "command/handlers/ExportHandler.h"
#include "imgui.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace ui {

ExportDialog::ExportDialog(catalog::PhotoRepository& repo) : repo_(repo) {}

void ExportDialog::open(int64_t primaryId, std::vector<int64_t> ids) {
  primaryId_   = primaryId;
  selectedIds_ = std::move(ids);
  open_        = true;

  // Reset handler state for a fresh export session.
  if (handler_) { handler_->reset(); }

  // Load persisted folder from DB on first open, then fall back to Desktop
  if (targetPath_.empty()) {
    targetPath_ = repo_.getSetting("export_target_path", "");
  }
  if (targetPath_.empty()) {
    targetPath_ = util::desktopDir();
  }
}

void ExportDialog::close() {
  if (handler_) { handler_->cancel(); }
  open_ = false;
}

void ExportDialog::startExport() {
  if (!handler_ || !registry_) { return; }

  nlohmann::json ids = nlohmann::json::array();
  for (const int64_t id : selectedIds_) { ids.push_back(id); }

  registry_->dispatch("export.photos", {
      {"ids",        ids},
      {"targetPath", targetPath_},
      {"quality",    90}
  });
}

void ExportDialog::render() {
  if (!open_) {
    return;
  }

  const bool exporting = handler_ && handler_->isRunning();
  const bool finished  = handler_ && handler_->isFinished();

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

  if (!exporting && !finished) {
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
        repo_.setSetting("export_target_path", targetPath_);
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

  } else if (exporting) {
    const int done  = handler_->doneCount();
    const int total = handler_->totalCount();
    const float prog = total > 0 ? static_cast<float>(done) / total : 0.f;
    ImGui::ProgressBar(prog, {-1, 0});
    ImGui::Text("%d / %d", done, total);
    if (ImGui::Button("Cancel##exp")) {
      handler_->cancel();
    }

  } else {
    ImGui::TextColored({0.2f, 1.f, 0.2f, 1.f}, "Export complete! %d exported, %d errors",
                       handler_->exportedCount(), handler_->errorCount());
    ImGui::Text("Files saved to: %s", targetPath_.c_str());
    if (ImGui::Button("Close")) {
      close();
    }
  }

  ImGui::End();
}

}  // namespace ui
