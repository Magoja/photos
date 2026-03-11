#pragma once
#include <string>
#include <vector>
#include <optional>

namespace util {

// Return path to ~/Library/Caches/PhotoLibrary/
std::string cacheDir();

// Return path to ~/Library/Application Support/PhotoLibrary/
std::string appSupportDir();

// Return path to ~/Library/Logs/PhotoLibrary/
std::string logsDir();

// Return path to user Desktop
std::string desktopDir();

// Open an NSOpenPanel for folder selection; returns chosen path or nullopt
std::optional<std::string> pickFolder();

// Open an NSOpenPanel for file selection (multiple); returns selected paths
std::vector<std::string> pickFiles(const std::vector<std::string>& extensions);

// Ensure directory exists (create if needed)
bool ensureDir(const std::string& path);

}  // namespace util
