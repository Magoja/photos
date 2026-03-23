#pragma once
#include "CommandResult.h"
#include <nlohmann/json.hpp>

namespace command {

class ICommandHandler {
 public:
  virtual ~ICommandHandler() = default;
  virtual CommandResult execute(nlohmann::json params) = 0;
};

}  // namespace command
