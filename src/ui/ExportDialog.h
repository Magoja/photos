#pragma once
#include "export/Exporter.h"
#include "catalog/PhotoRepository.h"
#include "util/Platform.h"
#include <vector>
#include <cstdint>
#include <memory>
#include <string>

namespace ui {

class ExportDialog {
 public:
  explicit ExportDialog(catalog::PhotoRepository& repo);

  // Open dialog for multi-photo export (primaryId sets the anchor; ids = all selected)
  void open(int64_t primaryId, std::vector<int64_t> ids);
  void close();
  bool isOpen() const { return open_; }

  void render();

 private:
  catalog::PhotoRepository& repo_;
  bool open_ = false;
  bool exporting_ = false;
  bool finished_ = false;

  int64_t primaryId_ = 0;
  std::vector<int64_t> selectedIds_;
  std::string targetPath_;
  int doneCount_ = 0;
  int totalCount_ = 0;
  int exportedCount_ = 0;
  int errorCount_ = 0;

  std::unique_ptr<export_ns::Exporter> exporter_;

  void startExport();
};

}  // namespace ui
