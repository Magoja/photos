#pragma once
#include "catalog/PhotoRepository.h"
#include "util/Platform.h"
#include <vector>
#include <cstdint>
#include <string>

namespace command { class CommandRegistry; class ExportHandler; }

namespace ui {

class ExportDialog {
 public:
  explicit ExportDialog(catalog::PhotoRepository& repo);

  // Open dialog for multi-photo export (primaryId sets the anchor; ids = all selected)
  void open(int64_t primaryId, std::vector<int64_t> ids);
  void close();
  bool isOpen() const { return open_; }

  void render();

  void setRegistry(const command::CommandRegistry* reg) { registry_ = reg; }
  void setHandler(command::ExportHandler*    handler) { handler_  = handler; }

 private:
  catalog::PhotoRepository& repo_;
  bool open_ = false;

  int64_t primaryId_ = 0;
  std::vector<int64_t> selectedIds_;
  std::string targetPath_;

  const command::CommandRegistry* registry_ = nullptr;
  command::ExportHandler*   handler_  = nullptr;

  void startExport();
};

}  // namespace ui
