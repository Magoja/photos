#pragma once
#include <string>
#include <cstdint>

namespace export_ns {

struct ExportPreset {
    int64_t     id         = 0;
    std::string name;
    int         quality    = 85;  // JPEG quality 1-100
    int         maxWidth   = 0;   // 0 = no limit
    int         maxHeight  = 0;
    std::string targetPath;
    std::string configJson = "{}";
};

} // namespace export_ns
