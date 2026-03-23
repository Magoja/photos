#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"
#include <functional>

namespace command {

// image.adjust — overlay adjust fields onto a photo's current EditSettings.
// Params (all optional except id):
//   id          : integer (required)
//   exposure    : number
//   temperature : number
//   contrast    : number
//   saturation  : number
// Only keys present in params are written; absent keys are left untouched.
//
// adjustedCb (optional): called with the photo id after DB write succeeds.
// The callback is the UI-layer hook for texture-cache invalidation.
class ImageAdjustHandler : public ICommandHandler {
 public:
  explicit ImageAdjustHandler(catalog::PhotoRepository& repo) : repo_(repo) {}
  ImageAdjustHandler(catalog::PhotoRepository& repo,
                     std::function<void(int64_t)> adjustedCb)
      : repo_(repo), adjustedCb_(std::move(adjustedCb)) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
  std::function<void(int64_t)> adjustedCb_;
};

}  // namespace command
