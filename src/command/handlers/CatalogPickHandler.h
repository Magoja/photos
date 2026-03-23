#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"
#include <functional>

namespace command {

// catalog.pick — update a photo's picked flag and fire a callback.
// Params:
//   id     : integer (required)
//   picked : integer 0 or 1 (required)
// pickedCb (optional): called with (id, picked) after DB write succeeds.
class CatalogPickHandler : public ICommandHandler {
 public:
  CatalogPickHandler(catalog::PhotoRepository& repo,
                     std::function<void(int64_t, int)> pickedCb)
      : repo_(repo), pickedCb_(std::move(pickedCb)) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
  std::function<void(int64_t, int)> pickedCb_;
};

}  // namespace command
