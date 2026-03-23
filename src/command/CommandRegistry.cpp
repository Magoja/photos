#include "CommandRegistry.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace command {

Dependencies defaultDependencies() {
  return {.log = [](bool ok, std::string_view line) {
    if (ok) {
      spdlog::info("{}", line);
    } else {
      spdlog::warn("{}", line);
    }
  }};
}

CommandRegistry::CommandRegistry(Dependencies deps) : deps_(std::move(deps)) {}

void CommandRegistry::registerHandler(std::string name,
                                      std::unique_ptr<ICommandHandler> handler) {
  if (!isValidCommandName(name)) {
    throw std::invalid_argument("invalid command name: '" + name +
                                "' (must be dot.namespaced lowercase)");
  }
  if (handlers_.contains(name)) {
    throw std::invalid_argument("duplicate command name: '" + name + "'");
  }
  handlers_[std::move(name)] = std::move(handler);
}

CommandResult CommandRegistry::dispatch(const std::string& name,
                                        nlohmann::json params) const {
  const auto it = handlers_.find(name);
  if (it == handlers_.end()) {
    const auto result = failure("unknown command: " + name);
    deps_.log(false, stringify(name, result));
    return result;
  }
  if (const auto v = it->second->validate(params); !v.has_value()) {
    const auto result = failure("invalid params for " + name + ": " + v.error());
    deps_.log(false, stringify(name, result));
    return result;
  }
  const auto result = it->second->execute(std::move(params));
  deps_.log(result.has_value(), stringify(name, result));
  return result;
}

}  // namespace command
