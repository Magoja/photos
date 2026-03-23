#pragma once
#include "command/ICommandHandler.h"
#include <functional>

namespace command {

// catalog.photo.open — fire a select callback for the given photo id.
// Params:
//   id : integer (required)
// selectCb (optional): called with id; no DB access.
class CatalogOpenHandler : public ICommandHandler {
 public:
  explicit CatalogOpenHandler(std::function<void(int64_t)> selectCb)
      : selectCb_(std::move(selectCb)) {}

  ValidationResult validate(const nlohmann::json& params) const override;
  CommandResult    execute(nlohmann::json params) override;

 private:
  std::function<void(int64_t)> selectCb_;
};

}  // namespace command
