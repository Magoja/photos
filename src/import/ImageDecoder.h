#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace import_ns {

struct DecodedImage {
  std::vector<uint8_t> rgb;
  int w = 0;
  int h = 0;
};

// Decode a source image file to a flat RGB (3 bytes/pixel) buffer.
// Selects JPEG (TurboJPEG + EXIF orientation) or RAW (LibRaw) path based on extension.
// Returns nullopt on any decode failure.
std::optional<DecodedImage> decodeSource(const std::string& srcPath);

}  // namespace import_ns
