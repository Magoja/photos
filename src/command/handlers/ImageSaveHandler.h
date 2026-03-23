#pragma once
#include "command/ICommandHandler.h"
#include "catalog/PhotoRepository.h"
#include <functional>

namespace command {

// image.save — write a complete EditSettings blob and fire a post-save callback.
// Params:
//   id       : integer (required)
//   settings : object  (required — full EditSettings as a JSON object)
// savedCb (optional): called with the photo id after DB write succeeds.
class ImageSaveHandler : public ICommandHandler {
 public:
  ImageSaveHandler(catalog::PhotoRepository& repo,
                   std::function<void(int64_t)> savedCb)
      : repo_(repo), savedCb_(std::move(savedCb)) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  catalog::PhotoRepository& repo_;
  std::function<void(int64_t)> savedCb_;
};

}  // namespace command
