#pragma once
#include <cctype>
#include <expected>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace command {

// Success carries a JSON data payload; failure carries an error string.
using CommandResult = std::expected<nlohmann::json, std::string>;

// Validates the dot-namespaced naming rule: one or more lowercase-alphanumeric
// segments separated by single dots. e.g. "image.adjust", "catalog.photo.open".
// No uppercase, no underscores, no consecutive/leading/trailing dots.
inline bool isValidCommandName(std::string_view name) {
  if (name.empty()) { return false; }
  bool hasDot = false;
  bool segmentStart = true;  // true at the start of each segment
  for (const char c : name) {
    if (c == '.') {
      if (segmentStart) { return false; }  // leading dot or consecutive dots
      hasDot = true;
      segmentStart = true;
    } else if (std::islower(static_cast<unsigned char>(c)) ||
               std::isdigit(static_cast<unsigned char>(c))) {
      segmentStart = false;
    } else {
      return false;  // disallowed character
    }
  }
  if (segmentStart) { return false; }  // trailing dot
  return hasDot;
}

// Result of validating handler input params: no payload on success, error string on failure.
using ValidationResult = std::expected<void, std::string>;

inline ValidationResult valid()                  { return {}; }
inline ValidationResult invalid(std::string msg) { return std::unexpected(std::move(msg)); }

inline CommandResult success(nlohmann::json data = {}) { return data; }
inline CommandResult failure(std::string msg) { return std::unexpected(std::move(msg)); }

// Returns a human-readable description of a dispatch result, e.g.:
//   "[CMD] image.adjust -> ok"
//   "[CMD] image.adjust -> error: unknown command: image.adjust"
inline std::string stringify(const std::string& name, const CommandResult& result) {
  if (result.has_value()) {
    return "[CMD] " + name + " -> ok";
  }
  return "[CMD] " + name + " -> error: " + result.error();
}

}  // namespace command
