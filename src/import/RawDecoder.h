#pragma once
#include "ExifParser.h"
#include <string>
#include <vector>
#include <cstdint>

namespace import_ns {

struct DecodeResult {
  std::vector<uint8_t> thumbJpeg;  // embedded JPEG thumbnail bytes
  ExifData exif;
  bool ok = false;
  std::string error;
};

class RawDecoder {
 public:
  // Decode a RAW (or JPEG) file: extract thumbnail + EXIF
  static DecodeResult decode(const std::string& filePath);
};

}  // namespace import_ns
