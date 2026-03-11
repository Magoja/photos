#include "FileScanner.h"
#include <filesystem>
#include <algorithm>
#include <ranges>
#include <cctype>

namespace fs = std::filesystem;

namespace import_ns {

const std::vector<std::string>& FileScanner::supportedExtensions() {
  static const std::vector<std::string> exts = {".cr3", ".cr2",  ".nef", ".arw", ".rw2", ".raf",
                                                ".orf", ".dng",  ".raw", ".pef", ".srw", ".x3f",
                                                ".jpg", ".jpeg", ".tif", ".tiff"};
  return exts;
}

static std::string toLower(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool FileScanner::isSupported(const std::string& ext) {
  const auto lower = toLower(ext);
  return std::ranges::any_of(supportedExtensions(),
                             [&](const std::string& e) { return e == lower; });
}

std::vector<ScannedFile> FileScanner::scan(const std::string& rootPath,
                                           std::function<void(const std::string&)> progress) {
  std::vector<ScannedFile> result;
  std::error_code ec;
  fs::recursive_directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec);
  if (ec)
    return result;

  for (auto& entry : it) {
    if (!entry.is_regular_file(ec))
      continue;
    auto& p = entry.path();
    if (!isSupported(p.extension().string()))
      continue;

    ScannedFile sf;
    sf.path = p.string();
    sf.size = static_cast<int64_t>(entry.file_size(ec));
    result.push_back(sf);

    if (progress)
      progress(sf.path);
  }
  return result;
}

}  // namespace import_ns
