#pragma once
#include "CommandResult.h"
#include "ICommandHandler.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace command {

class CommandRegistry {
 public:
  void registerHandler(std::string name, std::shared_ptr<ICommandHandler> handler);
  CommandResult dispatch(const std::string& name, nlohmann::json params = {});

 private:
  std::unordered_map<std::string, std::shared_ptr<ICommandHandler>> handlers_;
};

}  // namespace command
