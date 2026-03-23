#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"
#include <functional>

namespace command {

// image.revert — clear all edit_settings for a photo (write "{}").
// Params:
//   id : integer (required)
//
// adjustedCb (optional): called with the photo id after DB write succeeds.
// The callback is the UI-layer hook for texture-cache invalidation.
class ImageRevertHandler : public ICommandHandler {
 public:
  explicit ImageRevertHandler(catalog::PhotoRepository& repo) : repo_(repo) {}
  ImageRevertHandler(catalog::PhotoRepository& repo,
                     std::function<void(int64_t)> adjustedCb)
      : repo_(repo), adjustedCb_(std::move(adjustedCb)) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
  std::function<void(int64_t)> adjustedCb_;
};

}  // namespace command
