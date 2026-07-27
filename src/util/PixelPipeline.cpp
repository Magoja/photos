#include "PixelPipeline.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace util {

std::vector<uint8_t> downsampleRgb(const uint8_t* src, int srcW, int srcH, int scale, int& outW,
                                   int& outH) {
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
          sumR += src[idx];
          sumG += src[idx + 1];
          sumB += src[idx + 2];
        }
      }
      const int out = (y * outW + x) * 3;
      dst[out] = static_cast<uint8_t>(sumR / count);
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

std::vector<uint8_t> rgbaToRgb(const std::vector<uint8_t>& rgba, int pixelCount) {
  std::vector<uint8_t> rgb;
  rgb.reserve(static_cast<size_t>(pixelCount) * 3);
  for (int i = 0; i < pixelCount; ++i) {
    rgb.push_back(rgba[i * 4 + 0]);
    rgb.push_back(rgba[i * 4 + 1]);
    rgb.push_back(rgba[i * 4 + 2]);
  }
  return rgb;
}

float computeLuma(const uint8_t* rgb, int pixelCount) {
  if (pixelCount <= 0) {
    return 0.f;
  }
  double sum = 0.0;
  for (int i = 0; i < pixelCount; ++i) {
    sum += 0.299 * rgb[i * 3] + 0.587 * rgb[i * 3 + 1] + 0.114 * rgb[i * 3 + 2];
  }
  return static_cast<float>(sum / pixelCount);
}

// ── sRGB transfer (standard piecewise curve), operating on [0,1] ──────────────

static float linearFromSrgb(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static float srgbFromLinear(float c) {
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

// Decode table: input byte → linear-light value. Pure function of the byte, so
// computed once.
static const std::array<float, 256>& srgbDecodeTable() {
  static const std::array<float, 256> table = [] {
    std::array<float, 256> t{};
    for (int v = 0; v < 256; ++v) {
      t[v] = linearFromSrgb(v / 255.f);
    }
    return t;
  }();
  return table;
}

// Build a per-channel byte→sRGB-byte LUT (as float, to preserve precision for the
// subsequent gamma-space contrast/saturation): decode to linear, apply the
// exposure+white-balance gain, hard-clip to [0,1], re-encode to sRGB.
static std::array<float, 256> buildExposureWbLut(float gain) {
  std::array<float, 256> lut{};
  const auto& decode = srgbDecodeTable();
  for (int v = 0; v < 256; ++v) {
    lut[v] = srgbFromLinear(std::clamp(decode[v] * gain, 0.f, 1.f)) * 255.f;
  }
  return lut;
}

std::vector<uint8_t> applyAdjustments(const std::vector<uint8_t>& src, int w, int h,
                                      const catalog::EditSettings& s) {
  const float t = s.temperature / 100.f;
  const float eMul = std::pow(2.f, s.exposure);

  // Exposure and white balance are light-domain gains → apply in linear light.
  // When both are neutral, use an identity LUT so the result is byte-exact (no
  // sRGB round-trip drift).
  const bool lightNeutral = (s.exposure == 0.f && s.temperature == 0.f);
  std::array<float, 256> identity{};
  for (int v = 0; v < 256; ++v) {
    identity[v] = static_cast<float>(v);
  }
  const std::array<float, 256> lutR =
    lightNeutral ? identity : buildExposureWbLut(eMul * (1.f + t * 0.30f));
  const std::array<float, 256> lutG =
    lightNeutral ? identity : buildExposureWbLut(eMul * (1.f + t * 0.05f));
  const std::array<float, 256> lutB =
    lightNeutral ? identity : buildExposureWbLut(eMul * (1.f - t * 0.30f));

  // Contrast and saturation are perceptual "look" controls → applied in gamma space.
  const float cFact = 1.f + s.contrast / 100.f;
  const float sFact = 1.f + s.saturation / 100.f;

  std::vector<uint8_t> dst(src.size());
  const int n = w * h;
  for (int i = 0; i < n; ++i) {
    float r = lutR[src[i * 3 + 0]];
    float g = lutG[src[i * 3 + 1]];
    float b = lutB[src[i * 3 + 2]];

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

std::vector<uint8_t> rotateCropBuffer(const std::vector<uint8_t>& src, int w, int h,
                                      float angleDeg) {
  if (angleDeg == 0.f) {
    return src;
  }
  const float rad = angleDeg * (float)M_PI / 180.f;
  const float cosA = std::cos(-rad);
  const float sinA = std::sin(-rad);
  const float cx = w * 0.5f;
  const float cy = h * 0.5f;

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
        return {(float)src[idx], (float)src[idx + 1], (float)src[idx + 2]};
      };
      const auto s00 = sample(x0, y0);
      const auto s10 = sample(x0 + 1, y0);
      const auto s01 = sample(x0, y0 + 1);
      const auto s11 = sample(x0 + 1, y0 + 1);

      const int outIdx = (y * w + x) * 3;
      for (int c = 0; c < 3; ++c) {
        const float val = s00[c] * (1.f - fx) * (1.f - fy) + s10[c] * fx * (1.f - fy) +
                          s01[c] * (1.f - fx) * fy + s11[c] * fx * fy;
        dst[outIdx + c] = (uint8_t)std::clamp(val, 0.f, 255.f);
      }
    }
  }
  return dst;
}

std::vector<uint8_t> cropAndRotatePixels(const std::vector<uint8_t>& src, int srcW, int srcH,
                                         const catalog::CropRect& crop, int& outW, int& outH) {
  const int cropX = (int)(crop.x * srcW);
  const int cropY = (int)(crop.y * srcH);
  outW = std::max(1, (int)(crop.w * srcW));
  outH = std::max(1, (int)(crop.h * srcH));

  if (crop.angleDeg == 0.f) {
    std::vector<uint8_t> result(outW * outH * 3);
    for (int y = 0; y < outH; ++y) {
      const int srcRow = std::clamp(cropY + y, 0, srcH - 1);
      const int dstOff = y * outW * 3;
      const int srcOff = (srcRow * srcW + std::clamp(cropX, 0, srcW - 1)) * 3;
      const int copyW = std::min(outW, srcW - std::clamp(cropX, 0, srcW - 1));
      if (copyW > 0) {
        std::copy_n(src.begin() + srcOff, copyW * 3, result.begin() + dstOff);
      }
    }
    return result;
  }

  // Single-pass rotate-then-crop: for each output pixel, find its position in
  // full-image rotated space, then inverse-rotate around the full image center
  // to sample from the original. This gives the rotation access to all source
  // pixels, preventing edge-stretching and content loss at crop corners.
  const float rad = crop.angleDeg * (float)M_PI / 180.f;
  const float cosA = std::cos(rad);
  const float sinA = std::sin(rad);
  const float fcx = srcW * 0.5f;
  const float fcy = srcH * 0.5f;

  std::vector<uint8_t> result(outW * outH * 3);
  for (int y = 0; y < outH; ++y) {
    for (int x = 0; x < outW; ++x) {
      const float dx = (x + cropX) - fcx;
      const float dy = (y + cropY) - fcy;
      const float sx = cosA * dx + sinA * dy + fcx;
      const float sy = -sinA * dx + cosA * dy + fcy;

      const int x0 = (int)sx, y0 = (int)sy;
      const float fx = sx - x0, fy = sy - y0;
      auto sample = [&](int px, int py) -> std::array<float, 3> {
        px = std::clamp(px, 0, srcW - 1);
        py = std::clamp(py, 0, srcH - 1);
        const int idx = (py * srcW + px) * 3;
        return {(float)src[idx], (float)src[idx + 1], (float)src[idx + 2]};
      };
      const auto s00 = sample(x0, y0);
      const auto s10 = sample(x0 + 1, y0);
      const auto s01 = sample(x0, y0 + 1);
      const auto s11 = sample(x0 + 1, y0 + 1);
      const int outIdx = (y * outW + x) * 3;
      for (int c = 0; c < 3; ++c) {
        const float val = s00[c] * (1.f - fx) * (1.f - fy) + s10[c] * fx * (1.f - fy) +
                          s01[c] * (1.f - fx) * fy + s11[c] * fx * fy;
        result[outIdx + c] = (uint8_t)std::clamp(val, 0.f, 255.f);
      }
    }
  }
  return result;
}

std::vector<uint8_t> applyEditsToRgba(const std::vector<uint8_t>& rgb, int w, int h,
                                      const catalog::EditSettings& s, int& outW, int& outH) {
  const auto adjusted = applyAdjustments(rgb, w, h, s);
  const auto cropped = cropAndRotatePixels(adjusted, w, h, s.crop, outW, outH);
  return rgbToRgba(cropped, outW * outH);
}

}  // namespace util

// ── JPEG EXIF orientation helpers ─────────────────────────────────────────────

namespace {

uint16_t pipeU16(const uint8_t* p, bool be) {
  return be ? static_cast<uint16_t>(p[0] << 8 | p[1]) : static_cast<uint16_t>(p[1] << 8 | p[0]);
}

uint32_t pipeU32(const uint8_t* p, bool be) {
  return be ? (static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
               static_cast<uint32_t>(p[2]) << 8 | static_cast<uint32_t>(p[3]))
            : (static_cast<uint32_t>(p[3]) << 24 | static_cast<uint32_t>(p[2]) << 16 |
               static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[0]));
}

}  // namespace

namespace util {

Orientation readJpegOrientation(const uint8_t* data, size_t size) {
  if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
    return Orientation::Normal;
  }
  size_t pos = 2;
  while (pos + 4 <= size) {
    if (data[pos] != 0xFF) {
      break;
    }
    const uint8_t marker = data[pos + 1];
    const uint16_t segLen = static_cast<uint16_t>(data[pos + 2] << 8 | data[pos + 3]);
    if (segLen < 2) {
      break;
    }
    const size_t segEnd = pos + 2 + segLen;
    if (segEnd > size) {
      break;
    }

    if (marker == 0xE1 && segLen >= 8) {
      const uint8_t* seg = data + pos + 4;
      if (std::memcmp(seg, "Exif\0\0", 6) == 0) {
        const uint8_t* tiff = seg + 6;
        const size_t tiffSize = segLen - 2 - 6;
        if (tiffSize >= 8) {
          const bool be = (tiff[0] == 'M' && tiff[1] == 'M');
          if (pipeU16(tiff + 2, be) == 42) {
            const uint32_t ifd0Off = pipeU32(tiff + 4, be);
            if (ifd0Off + 2 <= tiffSize) {
              const uint16_t nEntries = pipeU16(tiff + ifd0Off, be);
              for (uint16_t e = 0; e < nEntries; ++e) {
                const size_t eOff = ifd0Off + 2 + static_cast<size_t>(e) * 12;
                if (eOff + 12 > tiffSize) {
                  break;
                }
                if (pipeU16(tiff + eOff, be) == 0x0112) {
                  return static_cast<Orientation>(pipeU16(tiff + eOff + 8, be));
                }
              }
            }
          }
        }
      }
    }
    if (marker == 0xDA) {
      break;
    }  // SOS marker — image data begins
    pos = segEnd;
  }
  return Orientation::Normal;
}

// ── Per-rotation helpers ──────────────────────────────────────────────────────

static void rotate180(std::vector<uint8_t>& rgb, int w, int h) {
  const int n = w * h;
  for (int i = 0, j = n - 1; i < j; ++i, --j) {
    std::swap(rgb[i * 3 + 0], rgb[j * 3 + 0]);
    std::swap(rgb[i * 3 + 1], rgb[j * 3 + 1]);
    std::swap(rgb[i * 3 + 2], rgb[j * 3 + 2]);
  }
}

static std::vector<uint8_t> rotate90CW(const std::vector<uint8_t>& rgb, int srcW, int srcH) {
  // dst[r][c] = src[srcH-1-c][r];  dstW=srcH, dstH=srcW
  std::vector<uint8_t> dst(static_cast<size_t>(srcW * srcH) * 3);
  for (int r = 0; r < srcW; ++r) {
    for (int c = 0; c < srcH; ++c) {
      const int srcIdx = ((srcH - 1 - c) * srcW + r) * 3;
      const int dstIdx = (r * srcH + c) * 3;
      dst[dstIdx + 0] = rgb[srcIdx + 0];
      dst[dstIdx + 1] = rgb[srcIdx + 1];
      dst[dstIdx + 2] = rgb[srcIdx + 2];
    }
  }
  return dst;
}

static std::vector<uint8_t> rotate90CCW(const std::vector<uint8_t>& rgb, int srcW, int srcH) {
  // dst[r][c] = src[c][srcW-1-r];  dstW=srcH, dstH=srcW
  std::vector<uint8_t> dst(static_cast<size_t>(srcW * srcH) * 3);
  for (int r = 0; r < srcW; ++r) {
    for (int c = 0; c < srcH; ++c) {
      const int srcIdx = (c * srcW + (srcW - 1 - r)) * 3;
      const int dstIdx = (r * srcH + c) * 3;
      dst[dstIdx + 0] = rgb[srcIdx + 0];
      dst[dstIdx + 1] = rgb[srcIdx + 1];
      dst[dstIdx + 2] = rgb[srcIdx + 2];
    }
  }
  return dst;
}

// ── Public apply ──────────────────────────────────────────────────────────────

void applyOrientationRgb(std::vector<uint8_t>& rgb, int& w, int& h, Orientation orientation) {
  switch (orientation) {
    case Orientation::Normal:
      break;
    case Orientation::Rotate180:
      rotate180(rgb, w, h);
      break;
    case Orientation::Rotate90CW:
      rgb = rotate90CW(rgb, w, h);
      std::swap(w, h);
      break;
    case Orientation::Rotate90CCW:
      rgb = rotate90CCW(rgb, w, h);
      std::swap(w, h);
      break;
    default:
      break;
  }
}

}  // namespace util
