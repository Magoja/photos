#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"

namespace command {

// image.crop — overlay crop sub-fields onto a photo's current EditSettings.
// Params (all optional except id):
//   id       : integer (required)
//   x, y, w, h, angleDeg : number
// Only keys present in params are written; absent keys are left untouched.
// Adjust fields (exposure, temperature, etc.) are never modified.
class ImageCropHandler : public ICommandHandler {
 public:
  explicit ImageCropHandler(catalog::PhotoRepository& repo) : repo_(repo) {}
  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
};

}  // namespace command
