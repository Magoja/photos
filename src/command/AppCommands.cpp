#include "command/AppCommands.h"

#include "catalog/PhotoRepository.h"
#include "command/handlers/ImageAdjustHandler.h"
#include "command/handlers/ImageRevertHandler.h"
#include "command/handlers/ImageCropHandler.h"
#include "command/handlers/ImageSaveHandler.h"
#include "command/handlers/CatalogPickHandler.h"
#include "command/handlers/CatalogOpenHandler.h"
#include "command/handlers/MetaSyncHandler.h"
#include "command/handlers/ExportHandler.h"
#include "export/ExportSession.h"
#include "ui/TextureManager.h"
#include "ui/GridView.h"

#include <memory>

namespace command {

CommandRegistry buildRegistry(catalog::PhotoRepository& repo,
                               ui::TextureManager&       texMgr,
                               ui::GridView&             grid,
                               export_ns::ExportSession& session) {
  CommandRegistry registry;

  // image.* — image.save's savedCb is null; EditView::pollSaveCompletion fires
  // the grid reload after thumbnail regen completes, not immediately on save.
  const auto evictBoth = [&](int64_t id) {
    texMgr.evict(id);
    texMgr.evict(id + ui::GridView::kMicroOffset);
  };
  registry.registerHandler("image.adjust",
      std::make_unique<ImageAdjustHandler>(repo, evictBoth));
  registry.registerHandler("image.revert",
      std::make_unique<ImageRevertHandler>(repo, evictBoth));
  registry.registerHandler("image.crop",
      std::make_unique<ImageCropHandler>(repo));
  registry.registerHandler("image.save",
      std::make_unique<ImageSaveHandler>(repo, nullptr));

  // catalog.*
  registry.registerHandler("catalog.pick",
      std::make_unique<CatalogPickHandler>(repo,
          [&](int64_t /*id*/, int /*picked*/) { grid.reload(); }));
  registry.registerHandler("catalog.photo.open",
      std::make_unique<CatalogOpenHandler>(nullptr));

  // metasync — doneCb is null; MetaSyncDialog fires its own doneCb from render()
  registry.registerHandler("metasync.apply",
      std::make_unique<MetaSyncHandler>(repo, nullptr));

  // export
  registry.registerHandler("export.photos",
      std::make_unique<ExportHandler>(session));

  return registry;
}

}  // namespace command
