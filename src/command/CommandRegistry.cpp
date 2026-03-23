#include "CommandRegistry.h"
#include <spdlog/spdlog.h>

namespace command {

void CommandRegistry::registerHandler(std::string name,
                                      std::shared_ptr<ICommandHandler> handler) {
  handlers_[std::move(name)] = std::move(handler);
}

CommandResult CommandRegistry::dispatch(const std::string& name, nlohmann::json params) {
  const auto it = handlers_.find(name);
  if (it == handlers_.end()) {
    const auto result = CommandResult::failure("unknown command: " + name);
    spdlog::warn("[CMD] {} -> error: {}", name, result.error);
    return result;
  }
  const auto result = it->second->execute(std::move(params));
  if (result.ok) {
    spdlog::info("[CMD] {} -> ok", name);
  } else {
    spdlog::warn("[CMD] {} -> error: {}", name, result.error);
  }
  return result;
}

}  // namespace command
