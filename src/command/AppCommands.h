#pragma once
#include "command/CommandRegistry.h"

namespace catalog { class PhotoRepository; }
namespace export_ns { class ExportSession; }
namespace ui {
  class TextureManager;
  class GridView;
}

namespace command {

// Constructs and returns a CommandRegistry with all application handlers registered.
CommandRegistry buildRegistry(catalog::PhotoRepository& repo,
                               ui::TextureManager&       texMgr,
                               ui::GridView&             grid,
                               export_ns::ExportSession& session);

}  // namespace command
