#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace command {

struct CommandResult {
  bool ok = false;
  std::string error;
  nlohmann::json data;

  static CommandResult success(nlohmann::json data = {}) {
    return {true, {}, std::move(data)};
  }
  static CommandResult failure(std::string err) {
    return {false, std::move(err), {}};
  }
};

}  // namespace command
