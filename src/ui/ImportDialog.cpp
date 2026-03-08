#include "ImportDialog.h"
#include "imgui.h"
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace ui {

ImportDialog::ImportDialog(catalog::Database& db) : db_(db) {}

void ImportDialog::open(const std::string& sourcePath,
                         const std::string& destPath,
                         const std::string& thumbCacheRoot)
{
    sourcePath_ = sourcePath;
    destPath_   = destPath;
    thumbRoot_  = thumbCacheRoot;
    open_       = true;
    importing_  = false;
    finished_   = false;
    doneFiles_  = 0;
    totalFiles_ = 0;

    // Count files for preview
    import_ns::ImportOptions opts;
    opts.sourcePath     = sourcePath_;
    opts.destPath       = destPath_;
    opts.thumbCacheRoot = thumbRoot_;

    importer_ = std::make_unique<import_ns::Importer>(db_, opts);
    importer_->setProgressCallback([this](int done, int total, const std::string& file) {
        doneFiles_  = done;
        totalFiles_ = total;
        currentFile_= fs::path(file).filename().string();
    });
    importer_->setDoneCallback([this](const import_ns::ImportStats& s) {
        stats_     = s;
        finished_  = true;
        importing_ = false;
        if (doneCb_) doneCb_();
    });
}

void ImportDialog::close() {
    if (importer_) importer_->cancel();
    open_ = false;
}

void ImportDialog::render() {
    if (!open_) return;

    ImGui::SetNextWindowSize({520, 320}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Import Photos##dlg", &open_)) { ImGui::End(); return; }

    ImGui::Text("Source: %s", sourcePath_.c_str());
    ImGui::Text("Destination: %s", destPath_.c_str());
    ImGui::Separator();

    if (!importing_ && !finished_) {
        if (ImGui::Button("Start Import")) {
            importing_ = true;
            importer_->start();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##pre")) close();
    } else if (importing_) {
        float progress = totalFiles_ > 0
            ? (float)doneFiles_ / totalFiles_ : 0.f;
        ImGui::Text("Importing: %s", currentFile_.c_str());
        ImGui::ProgressBar(progress, {-1, 0});
        ImGui::Text("%d / %d files", doneFiles_, totalFiles_);
        ImGui::Separator();
        if (ImGui::Button("Cancel##imp")) {
            importer_->cancel();
        }
    } else {
        // Finished
        ImGui::TextColored({0.2f,1.f,0.2f,1.f},
            "Done!  %d new,  %d duplicates,  %d errors",
            stats_.imported, stats_.duplicates, stats_.errors);
        if (ImGui::Button("Close")) close();
    }

    ImGui::End();
}

} // namespace ui
