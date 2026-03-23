#pragma once
#include "catalog/EditSettings.h"
#include <optional>
#include <string>
#include <vector>

namespace ui {

// Decoded-and-adjusted thumbnail pixels, ready for TextureManager::uploadRgba.
struct ThumbPixels { std::vector<uint8_t> rgba; int w; int h; };

// Decode thumb JPEG at thumbPath, apply EditSettings adjustments (exposure,
// temperature, contrast, saturation, crop), and return RGBA pixels + dims.
// Returns nullopt if the file is missing or decode fails. Thread-safe (no Metal).
std::optional<ThumbPixels> applyEditsToThumb(
    const std::string& thumbPath, const catalog::EditSettings& s);

}  // namespace ui
