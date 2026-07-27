#include "ThumbEditApplier.h"
#include "TextureManager.h"
#include "util/PixelPipeline.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace ui {

std::optional<ThumbPixels> applyEditsToThumb(const std::string& thumbPath,
                                             const catalog::EditSettings& s) {
  std::ifstream f(thumbPath, std::ios::binary);
  if (!f) {
    return std::nullopt;
  }
  const std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(f)), {});
  if (jpeg.empty()) {
    return std::nullopt;
  }

  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  if (!TextureManager::decodeJpeg(jpeg, rgba, w, h)) {
    return std::nullopt;
  }

  // Reuse the canonical pipeline so the grid thumbnail matches the EditView
  // preview / export exactly. The pipeline works on RGB, so round-trip through it.
  const int pixelCount = w * h;
  const auto adjustedRgb = util::applyAdjustments(util::rgbaToRgb(rgba, pixelCount), w, h, s);
  rgba = util::rgbToRgba(adjustedRgb, pixelCount);

  // Apply crop
  const int cropX = static_cast<int>(s.crop.x * w);
  const int cropY = static_cast<int>(s.crop.y * h);
  const int cropW = std::max(1, static_cast<int>(s.crop.w * w));
  const int cropH = std::max(1, static_cast<int>(s.crop.h * h));

  if (cropX == 0 && cropY == 0 && cropW == w && cropH == h) {
    return ThumbPixels{std::move(rgba), w, h};
  }

  std::vector<uint8_t> cropped(static_cast<size_t>(cropW) * cropH * 4);
  for (int row = 0; row < cropH; ++row) {
    const int srcRow = std::clamp(cropY + row, 0, h - 1);
    const int srcCol = std::clamp(cropX, 0, w - 1);
    const int copyW = std::min(cropW, w - srcCol);
    std::memcpy(cropped.data() + static_cast<size_t>(row) * cropW * 4,
                rgba.data() + (static_cast<size_t>(srcRow) * w + srcCol) * 4,
                static_cast<size_t>(copyW) * 4);
  }
  return ThumbPixels{std::move(cropped), cropW, cropH};
}

}  // namespace ui
