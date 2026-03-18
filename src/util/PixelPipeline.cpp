#include "PixelPipeline.h"
#include <cmath>
#include <algorithm>

namespace util {

std::vector<uint8_t> applyAdjustments(const std::vector<uint8_t>& src,
                                       int w, int h,
                                       const catalog::EditSettings& s) {
  const float eMul  = std::pow(2.f, s.exposure);
  const float t     = s.temperature / 100.f;
  const float rMul  = 1.f + t * 0.30f;
  const float gMul  = 1.f + t * 0.05f;
  const float bMul  = 1.f - t * 0.30f;
  const float cFact = 1.f + s.contrast   / 100.f;
  const float sFact = 1.f + s.saturation / 100.f;

  std::vector<uint8_t> dst(src.size());
  const int n = w * h;
  for (int i = 0; i < n; ++i) {
    float r = src[i * 3 + 0];
    float g = src[i * 3 + 1];
    float b = src[i * 3 + 2];

    r *= eMul;  g *= eMul;  b *= eMul;
    r *= rMul;  g *= gMul;  b *= bMul;

    r = 128.f + (r - 128.f) * cFact;
    g = 128.f + (g - 128.f) * cFact;
    b = 128.f + (b - 128.f) * cFact;

    const float L = 0.299f * r + 0.587f * g + 0.114f * b;
    r = L + (r - L) * sFact;
    g = L + (g - L) * sFact;
    b = L + (b - L) * sFact;

    // Round-to-nearest instead of truncate; avoids a systematic darkening bias
    // when intermediate float results fall just below an integer boundary
    // (e.g. 100 × 1.05f = 104.999…, which truncation would render as 104).
    dst[i * 3 + 0] = static_cast<uint8_t>(std::lround(std::clamp(r, 0.f, 255.f)));
    dst[i * 3 + 1] = static_cast<uint8_t>(std::lround(std::clamp(g, 0.f, 255.f)));
    dst[i * 3 + 2] = static_cast<uint8_t>(std::lround(std::clamp(b, 0.f, 255.f)));
  }
  return dst;
}

}  // namespace util
