#pragma once
#include "catalog/PhotoRepository.h"
#include <cstdint>
#include <vector>

namespace export_ns {

// Build a complete EXIF APP1 payload (the "Exif\0\0" header followed by a
// little-endian TIFF block) from a photo record's metadata: camera make/model,
// capture time (DateTimeOriginal), and GPS position when present.
std::vector<uint8_t> buildExifPayload(const catalog::PhotoRecord& rec);

// Insert an APP1 EXIF segment immediately after the JPEG SOI marker (FF D8).
// Returns the input unchanged when it is not a valid JPEG (missing SOI).
std::vector<uint8_t> injectExifApp1(const std::vector<uint8_t>& jpeg,
                                    const std::vector<uint8_t>& exifPayload);

}  // namespace export_ns
