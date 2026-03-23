#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"
#include <functional>

namespace command {

// metasync.apply — propagate edit settings from a primary photo to target photos.
// Params:
//   primaryId  : integer (required) — source photo
//   targetIds  : array of integers (required) — destination photos (primary excluded)
//   syncAdjust : bool (default false) — copy exposure/temperature/contrast/saturation
//   syncCrop   : bool (default false) — copy crop rect
// All writes are committed in a single DB transaction before doneCb fires.
class MetaSyncHandler : public ICommandHandler {
 public:
  MetaSyncHandler(catalog::PhotoRepository& repo, std::function<void()> doneCb)
      : repo_(repo), doneCb_(std::move(doneCb)) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
  std::function<void()> doneCb_;
};

}  // namespace command
