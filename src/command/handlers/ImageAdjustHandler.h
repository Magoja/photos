#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"

namespace command {

// image.adjust — overlay adjust fields onto a photo's current EditSettings.
// Params (all optional except id):
//   id          : integer (required)
//   exposure    : number
//   temperature : number
//   contrast    : number
//   saturation  : number
// Only keys present in params are written; absent keys are left untouched.
class ImageAdjustHandler : public ICommandHandler {
 public:
  explicit ImageAdjustHandler(catalog::PhotoRepository& repo) : repo_(repo) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
};

}  // namespace command
