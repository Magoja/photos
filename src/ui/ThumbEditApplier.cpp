#include "ThumbEditApplier.h"
#include "TextureManager.h"
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ui {

std::optional<ThumbPixels> applyEditsToThumb(
    const std::string& thumbPath, const catalog::EditSettings& s) {
  std::ifstream f(thumbPath, std::ios::binary);
  if (!f) { return std::nullopt; }
  const std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(f)), {});
  if (jpeg.empty()) { return std::nullopt; }

  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  if (!TextureManager::decodeJpeg(jpeg, rgba, w, h)) { return std::nullopt; }

  const float eMul  = std::pow(2.f, s.exposure);
  const float t     = s.temperature / 100.f;
  const float rMul  = 1.f + t * 0.30f;
  const float gMul  = 1.f + t * 0.05f;
  const float bMul  = 1.f - t * 0.30f;
  const float cFact = 1.f + s.contrast   / 100.f;
  const float sFact = 1.f + s.saturation / 100.f;

  for (int i = 0, n = w * h; i < n; ++i) {
    float r = rgba[i * 4 + 0];
    float g = rgba[i * 4 + 1];
    float b = rgba[i * 4 + 2];

    r *= eMul;  g *= eMul;  b *= eMul;
    r *= rMul;  g *= gMul;  b *= bMul;
    r = 128.f + (r - 128.f) * cFact;
    g = 128.f + (g - 128.f) * cFact;
    b = 128.f + (b - 128.f) * cFact;
    const float L = 0.299f * r + 0.587f * g + 0.114f * b;
    r = L + (r - L) * sFact;
    g = L + (g - L) * sFact;
    b = L + (b - L) * sFact;

    rgba[i * 4 + 0] = static_cast<uint8_t>(std::clamp(r, 0.f, 255.f));
    rgba[i * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0.f, 255.f));
    rgba[i * 4 + 2] = static_cast<uint8_t>(std::clamp(b, 0.f, 255.f));
  }

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
    const int copyW  = std::min(cropW, w - srcCol);
    std::memcpy(cropped.data() + static_cast<size_t>(row) * cropW * 4,
                rgba.data() + (static_cast<size_t>(srcRow) * w + srcCol) * 4,
                static_cast<size_t>(copyW) * 4);
  }
  return ThumbPixels{std::move(cropped), cropW, cropH};
}

}  // namespace ui
