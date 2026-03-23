#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"

namespace command {

// image.revert — clear all edit_settings for a photo (write "{}").
// Params:
//   id : integer (required)
class ImageRevertHandler : public ICommandHandler {
 public:
  explicit ImageRevertHandler(catalog::PhotoRepository& repo) : repo_(repo) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
};

}  // namespace command
