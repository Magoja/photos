#pragma once
#include "catalog/PhotoRepository.h"
#include "export/ExportSession.h"
#include "util/Platform.h"
#include <vector>
#include <cstdint>
#include <string>

namespace ui {

class ExportDialog {
 public:
  ExportDialog(catalog::PhotoRepository& repo, export_ns::ExportSession& session);

  // Open dialog for multi-photo export (primaryId sets the anchor; ids = all selected)
  void open(int64_t primaryId, std::vector<int64_t> ids);
  void close();
  bool isOpen() const { return open_; }

  void render();

 private:
  catalog::PhotoRepository& repo_;
  export_ns::ExportSession& session_;
  bool open_ = false;

  int64_t primaryId_ = 0;
  std::vector<int64_t> selectedIds_;
  std::string targetPath_;

  void startExport();
};

}  // namespace ui
