#pragma once
#include <string>
#include <vector>
#include <functional>

namespace import_ns {

struct ScannedFile {
    std::string path;
    int64_t     size = 0;
};

class FileScanner {
public:
    // Supported RAW + JPEG extensions (lowercase)
    static const std::vector<std::string>& supportedExtensions();

    // Scan directory recursively; calls progress for each file found.
    static std::vector<ScannedFile> scan(
        const std::string& rootPath,
        std::function<void(const std::string&)> progress = nullptr);

    // Check if a file extension is supported
    static bool isSupported(const std::string& ext);
};

} // namespace import_ns
