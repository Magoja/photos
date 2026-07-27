#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace import_ns {

struct ExifData {
  std::string cameraMake;
  std::string cameraModel;
  std::string lensModel;
  std::string captureTime;  // ISO 8601: "YYYY-MM-DDTHH:MM:SS"
  double focalLengthMm = 0.0;
  double aperture = 0.0;
  std::string shutterSpeed;
  int iso = 0;
  int widthPx = 0;
  int heightPx = 0;
  double gpsLat = 0.0;
  double gpsLon = 0.0;
  double gpsAltM = 0.0;
};

// Parse the EXIF metadata from a JPEG file's raw bytes.
// Walks the JPEG marker stream for the APP1 "Exif\0\0" segment and reads the
// embedded TIFF/EXIF/GPS IFDs. Returns std::nullopt when the input is not a
// JPEG or carries no readable EXIF; all offsets are bounds-checked so malformed
// input yields nullopt (or partially-filled data) rather than a crash.
std::optional<ExifData> parseJpegExif(std::span<const uint8_t> fileBytes);

}  // namespace import_ns
