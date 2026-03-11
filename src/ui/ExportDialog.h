#pragma once
#include "export/Exporter.h"
#include "catalog/Database.h"
#include "catalog/PhotoRepository.h"
#include <vector>
#include <cstdint>
#include <memory>
#include <string>

namespace ui {

class ExportDialog {
 public:
  ExportDialog(catalog::PhotoRepository& repo);

  void open(const std::vector<int64_t>& selectedIds);
  void close();
  bool isOpen() const { return open_; }

  void render();

 private:
  catalog::PhotoRepository& repo_;
  bool open_ = false;
  bool exporting_ = false;
  bool finished_ = false;

  std::vector<int64_t> selectedIds_;
  std::vector<export_ns::ExportPreset> presets_;
  int selectedPreset_ = 0;
  std::string targetPath_;
  int doneCount_ = 0;
  int totalCount_ = 0;
  int exportedCount_ = 0;
  int errorCount_ = 0;

  std::unique_ptr<export_ns::Exporter> exporter_;

  void loadPresets();
};

}  // namespace ui
