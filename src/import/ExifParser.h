#pragma once
#include <string>
#include <cstdint>

namespace import_ns {

struct ExifData {
    std::string cameraMake;
    std::string cameraModel;
    std::string lensModel;
    std::string captureTime;   // ISO 8601: "YYYY-MM-DDTHH:MM:SS"
    double      focalLengthMm = 0.0;
    double      aperture      = 0.0;
    std::string shutterSpeed;
    int         iso           = 0;
    int         widthPx       = 0;
    int         heightPx      = 0;
    double      gpsLat        = 0.0;
    double      gpsLon        = 0.0;
    double      gpsAltM       = 0.0;
};

} // namespace import_ns
