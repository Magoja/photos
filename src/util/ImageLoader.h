#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace util {

struct RgbImage {
  std::vector<uint8_t> pixels;  // planar RGB, 3 bytes/pixel, row-major
  int width = 0;
  int height = 0;
  bool ok() const { return !pixels.empty() && width > 0 && height > 0; }
};

// Decode any supported image file (RAW or JPEG) to an RGB pixel buffer.
// RAW files: LibRaw full pipeline (open → unpack → dcraw_process → bitmap).
// Plain JPEG: libjpeg-turbo direct decompression (fallback when LibRaw rejects the file).
// maxEdge: if > 0, integer-downsample (÷N) so that max(w, h) ≤ maxEdge.
// Returns an empty RgbImage on any unrecoverable error.
RgbImage loadImageAsRgb(const std::string& path, int maxEdge = 0);

}  // namespace util
