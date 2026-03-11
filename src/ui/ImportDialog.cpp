#include "ImportDialog.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "util/Platform.h"
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace ui {

ImportDialog::ImportDialog(catalog::Database& db) : db_(db) {}

void ImportDialog::open(const std::string& sourcePath, const std::string& destPath,
                        const std::string& thumbCacheRoot) {
  sourcePath_ = sourcePath;
  destPath_ = destPath;
  thumbRoot_ = thumbCacheRoot;
  copyFiles_ = true;
  open_ = true;
  importing_ = false;
  finished_ = false;
  doneFiles_ = 0;
  totalFiles_ = 0;
  {
    std::lock_guard lk(progressMtx_);
    currentFile_.clear();
  }
  importer_.reset();
}

void ImportDialog::close() {
  if (importer_) {
    importer_->cancel();
  }
  open_ = false;
}

void ImportDialog::startImport() {
  import_ns::ImportOptions opts;
  opts.sourcePath = sourcePath_;
  opts.destPath = destPath_;
  opts.thumbCacheRoot = thumbRoot_;
  opts.copyFiles = copyFiles_;

  importer_ = std::make_unique<import_ns::Importer>(db_, opts);
  importer_->setProgressCallback([this](int done, int total, const std::string& file) {
    doneFiles_ = done;
    totalFiles_ = total;
    std::lock_guard lk(progressMtx_);
    currentFile_ = fs::path(file).filename().string();
  });
  importer_->setDoneCallback([this](const import_ns::ImportStats& s) {
    {
      std::lock_guard lk(progressMtx_);
      stats_ = s;
    }
    finished_ = true;
    importing_ = false;
    if (doneCb_) {
      doneCb_();
    }
  });
  importer_->start();
  importing_ = true;
}

void ImportDialog::render() {
  if (!open_) {
    return;
  }

  ImGui::SetNextWindowSize({560, 240}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
                          {0.5f, 0.5f});
  if (!ImGui::Begin("Import Photos##dlg", &open_)) {
    ImGui::End();
    return;
  }

  if (!importing_ && !finished_) {
    // Source row
    ImGui::Text("Source:");
    ImGui::SameLine(90);
    ImGui::SetNextItemWidth(360);
    ImGui::InputText("##src", &sourcePath_);
    ImGui::SameLine();
    if (ImGui::Button("Browse##src")) {
      auto picked = util::pickFolder();
      if (picked) {
        sourcePath_ = *picked;
        // Auto-uncheck copy if source is inside dest
        std::error_code ec;
        auto rel = fs::relative(sourcePath_, destPath_, ec);
        if (!ec && !rel.empty() && rel.native().find("..") == std::string::npos) {
          copyFiles_ = false;
        } else {
          copyFiles_ = true;
        }
      }
    }

    // Destination row
    ImGui::Text("Destination:");
    ImGui::SameLine(90);
    ImGui::SetNextItemWidth(360);
    ImGui::InputText("##dst", &destPath_);
    ImGui::SameLine();
    if (ImGui::Button("Browse##dst")) {
      auto picked = util::pickFolder();
      if (picked) {
        destPath_ = *picked;
      }
    }

    // Copy checkbox
    ImGui::Checkbox("Copy files to library", &copyFiles_);

    ImGui::Separator();

    if (ImGui::Button("Start Import")) {
      startImport();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##pre")) {
      close();
    }
  } else if (importing_) {
    int done = doneFiles_;
    int total = totalFiles_;
    std::string curFile;
    {
      std::lock_guard lk(progressMtx_);
      curFile = currentFile_;
    }
    float progress = total > 0 ? (float)done / total : 0.f;
    ImGui::Text("Importing: %s", curFile.c_str());
    ImGui::ProgressBar(progress, {-1, 0});
    ImGui::Text("%d / %d files", done, total);
    ImGui::Separator();
    if (ImGui::Button("Cancel##imp")) {
      importer_->cancel();
    }
  } else {
    // Finished
    ImGui::TextColored({0.2f, 1.f, 0.2f, 1.f}, "Done!  %d new,  %d duplicates,  %d errors",
                       stats_.imported, stats_.duplicates, stats_.errors);
    if (ImGui::Button("Close")) {
      close();
    }
  }

  ImGui::End();
}

}  // namespace ui
