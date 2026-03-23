#pragma once
#include "command/CommandRegistry.h"

namespace catalog { class PhotoRepository; }
namespace ui {
  class TextureManager;
  class GridView;
  class ExportDialog;
}

namespace command {

// Constructs and returns a CommandRegistry with all application handlers
// registered. As a side effect, wires the ExportHandler* into exportDlg so
// it can poll export progress.
CommandRegistry buildRegistry(catalog::PhotoRepository& repo,
                               ui::TextureManager&       texMgr,
                               ui::GridView&             grid,
                               ui::ExportDialog&         exportDlg);

}  // namespace command
