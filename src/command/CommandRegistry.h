#pragma once
#include "CommandResult.h"
#include "ICommandHandler.h"
#include <functional>
#include <stdexcept>
#include <memory>
#include <string>
#include <unordered_map>

namespace command {

struct Dependencies {
  // Called after every dispatch with a human-readable log line.
  // Defaults to spdlog; tests can inject a capturing lambda.
  std::function<void(bool ok, std::string_view line)> log;
};

// Returns a Dependencies wired to spdlog (info on success, warn on failure).
Dependencies defaultDependencies();

class CommandRegistry {
 public:
  explicit CommandRegistry(Dependencies deps = defaultDependencies());

  // Asserts: name passes isValidCommandName(); name not already registered.
  void registerHandler(std::string name, std::unique_ptr<ICommandHandler> handler);

  CommandResult dispatch(const std::string& name, nlohmann::json params = {}) const;

 private:
  Dependencies deps_;
  std::unordered_map<std::string, std::unique_ptr<ICommandHandler>> handlers_;
};

}  // namespace command
