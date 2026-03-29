#include "PixelPipeline.h"
#include <array>
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

std::vector<uint8_t> rotateCropBuffer(const std::vector<uint8_t>& src,
                                      int w, int h, float angleDeg) {
  if (angleDeg == 0.f) {
    return src;
  }
  const float rad  = angleDeg * (float)M_PI / 180.f;
  const float cosA = std::cos(-rad);
  const float sinA = std::sin(-rad);
  const float cx   = w * 0.5f;
  const float cy   = h * 0.5f;

  std::vector<uint8_t> dst(w * h * 3, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float dx = x - cx;
      const float dy = y - cy;
      const float sx = cosA * dx - sinA * dy + cx;
      const float sy = sinA * dx + cosA * dy + cy;

      const int x0 = (int)sx;
      const int y0 = (int)sy;
      const float fx = sx - x0;
      const float fy = sy - y0;

      auto sample = [&](int px, int py) -> std::array<float, 3> {
        px = std::clamp(px, 0, w - 1);
        py = std::clamp(py, 0, h - 1);
        const int idx = (py * w + px) * 3;
        return {(float)src[idx], (float)src[idx+1], (float)src[idx+2]};
      };
      const auto s00 = sample(x0,   y0);
      const auto s10 = sample(x0+1, y0);
      const auto s01 = sample(x0,   y0+1);
      const auto s11 = sample(x0+1, y0+1);

      const int outIdx = (y * w + x) * 3;
      for (int c = 0; c < 3; ++c) {
        const float val = s00[c]*(1.f-fx)*(1.f-fy) + s10[c]*fx*(1.f-fy)
                        + s01[c]*(1.f-fx)*fy        + s11[c]*fx*fy;
        dst[outIdx+c] = (uint8_t)std::clamp(val, 0.f, 255.f);
      }
    }
  }
  return dst;
}

std::vector<uint8_t> cropAndRotatePixels(const std::vector<uint8_t>& src,
                                         int srcW, int srcH,
                                         const catalog::CropRect& crop,
                                         int& outW, int& outH) {
  const int cropX = (int)(crop.x * srcW);
  const int cropY = (int)(crop.y * srcH);
  outW = std::max(1, (int)(crop.w * srcW));
  outH = std::max(1, (int)(crop.h * srcH));

  std::vector<uint8_t> cropped(outW * outH * 3);
  for (int y = 0; y < outH; ++y) {
    const int srcRow = std::clamp(cropY + y, 0, srcH - 1);
    const int dstOff = y * outW * 3;
    const int srcOff = (srcRow * srcW + std::clamp(cropX, 0, srcW - 1)) * 3;
    const int copyW  = std::min(outW, srcW - std::clamp(cropX, 0, srcW - 1));
    if (copyW > 0) {
      std::copy_n(src.begin() + srcOff, copyW * 3, cropped.begin() + dstOff);
    }
  }
  if (crop.angleDeg != 0.f) {
    cropped = rotateCropBuffer(cropped, outW, outH, crop.angleDeg);
  }
  return cropped;
}

}  // namespace util
