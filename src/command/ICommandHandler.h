#pragma once
#include "CommandResult.h"
#include <nlohmann/json.hpp>

namespace command {

class ICommandHandler {
 public:
  virtual ~ICommandHandler() = default;

  // Validate that params contain the expected fields and types.
  // Called by CommandRegistry::dispatch() before execute().
  // Return invalid("reason") to reject; valid() to proceed.
  virtual ValidationResult validate(const nlohmann::json& params) const = 0;

  virtual CommandResult execute(nlohmann::json params) = 0;
};

}  // namespace command
