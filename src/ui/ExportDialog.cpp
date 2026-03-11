#include "ExportDialog.h"
#include "imgui.h"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace ui {

ExportDialog::ExportDialog(catalog::PhotoRepository& repo) : repo_(repo) {
  loadPresets();
}

void ExportDialog::loadPresets() {
  presets_.clear();
  auto s = repo_.db().prepare(
    "SELECT id,name,quality,max_width,max_height,target_path,config_json"
    " FROM export_presets ORDER BY id");
  while (s.step()) {
    export_ns::ExportPreset p;
    p.id = s.getInt64(0);
    p.name = s.getText(1);
    p.quality = s.getInt(2);
    p.maxWidth = s.getInt(3);
    p.maxHeight = s.getInt(4);
    p.targetPath = s.getText(5);
    p.configJson = s.getText(6);
    presets_.push_back(p);
  }
}

void ExportDialog::open(const std::vector<int64_t>& selectedIds) {
  selectedIds_ = selectedIds;
  loadPresets();
  open_ = true;
  exporting_ = false;
  finished_ = false;
  doneCount_ = 0;
  totalCount_ = (int)selectedIds.size();
}

void ExportDialog::close() {
  if (exporter_) {
    exporter_->cancel();
  }
  open_ = false;
}

void ExportDialog::render() {
  if (!open_) {
    return;
  }

  ImGui::SetNextWindowSize({480, 280}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
                          {0.5f, 0.5f});
  if (!ImGui::Begin("Export Photos##dlg", &open_)) {
    ImGui::End();
    return;
  }

  ImGui::Text("%d photos selected", (int)selectedIds_.size());
  ImGui::Separator();

  // Preset selector
  if (!exporting_ && !finished_) {
    ImGui::Text("Preset:");
    for (int i = 0; i < (int)presets_.size(); ++i) {
      bool sel = (i == selectedPreset_);
      if (ImGui::RadioButton(presets_[i].name.c_str(), sel)) {
        selectedPreset_ = i;
      }
    }

    ImGui::Separator();
    ImGui::Text("Target folder:");
    char buf[512] = {};
    if (!presets_.empty()) {
      std::snprintf(buf, sizeof(buf), "%s", presets_[selectedPreset_].targetPath.c_str());
    }
    if (ImGui::InputText("##target", buf, sizeof(buf))) {
      if (!presets_.empty()) {
        presets_[selectedPreset_].targetPath = buf;
      }
    }

    ImGui::Separator();
    if (ImGui::Button("Export")) {
      if (!presets_.empty()) {
        auto& preset = presets_[selectedPreset_];
        // Save target_path to DB
        auto s = repo_.db().prepare("UPDATE export_presets SET target_path=? WHERE id=?");
        s.bind(1, preset.targetPath);
        s.bind(2, preset.id);
        s.step();

        exporter_ = std::make_unique<export_ns::Exporter>(repo_, preset);
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
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##pre")) {
      close();
    }

  } else if (exporting_) {
    float prog = totalCount_ > 0 ? (float)doneCount_ / totalCount_ : 0.f;
    ImGui::ProgressBar(prog, {-1, 0});
    ImGui::Text("%d / %d", doneCount_, totalCount_);
    if (ImGui::Button("Cancel##exp")) {
      exporter_->cancel();
    }

  } else {
    ImGui::TextColored({0.2f, 1.f, 0.2f, 1.f}, "Export complete! %d exported, %d errors",
                       exportedCount_, errorCount_);
    if (ImGui::Button("Close")) {
      close();
    }
  }

  ImGui::End();
}

}  // namespace ui
