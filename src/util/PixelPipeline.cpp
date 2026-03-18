#include "PixelPipeline.h"
#include <cmath>
#include <algorithm>

namespace util {

std::vector<uint8_t> downsampleRgb(const uint8_t* src, int srcW, int srcH,
                                    int scale, int& outW, int& outH) {
  outW = srcW / scale;
  outH = srcH / scale;
  std::vector<uint8_t> dst(static_cast<size_t>(outW * outH) * 3);
  const int count = scale * scale;
  for (int y = 0; y < outH; ++y) {
    for (int x = 0; x < outW; ++x) {
      int sumR = 0, sumG = 0, sumB = 0;
      for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) {
          const int idx = ((y * scale + dy) * srcW + (x * scale + dx)) * 3;
          sumR += src[idx]; sumG += src[idx + 1]; sumB += src[idx + 2];
        }
      }
      const int out = (y * outW + x) * 3;
      dst[out]     = static_cast<uint8_t>(sumR / count);
      dst[out + 1] = static_cast<uint8_t>(sumG / count);
      dst[out + 2] = static_cast<uint8_t>(sumB / count);
    }
  }
  return dst;
}

std::vector<uint8_t> rgbToRgba(const std::vector<uint8_t>& rgb, int pixelCount) {
  std::vector<uint8_t> rgba;
  rgba.reserve(static_cast<size_t>(pixelCount) * 4);
  for (int i = 0; i < pixelCount; ++i) {
    rgba.push_back(rgb[i * 3 + 0]);
    rgba.push_back(rgb[i * 3 + 1]);
    rgba.push_back(rgb[i * 3 + 2]);
    rgba.push_back(255);
  }
  return rgba;
}

float computeLuma(const uint8_t* rgb, int pixelCount) {
  if (pixelCount <= 0) { return 0.f; }
  double sum = 0.0;
  for (int i = 0; i < pixelCount; ++i) {
    sum += 0.299 * rgb[i * 3] + 0.587 * rgb[i * 3 + 1] + 0.114 * rgb[i * 3 + 2];
  }
  return static_cast<float>(sum / pixelCount);
}

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
