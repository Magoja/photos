#pragma once
#include "catalog/EditSettings.h"
#include <vector>
#include <cstdint>

namespace util {

// Exposure correction applied after every LibRaw decode to compensate for the
// ~1 EV brightness difference between LibRaw's neutral output and the
// camera-embedded JPEG (which has the camera ISP tone curve baked in).
// Applied in gamma-encoded space: each channel is multiplied by pow(2, kRawBoostEV).
// Consistent across EditView preview, FullscreenView preview, and Exporter so
// "what you see is what you export".
constexpr float kRawBoostEV = 1.0f;

// Apply kRawBoostEV exposure correction to a LibRaw-decoded RGB buffer.
// Call once after downsampleRgb() and before applyAdjustments() in every
// LibRaw decode path (EditView, FullscreenView, Exporter) to compensate for
// the ~1 EV brightness gap between LibRaw and camera-embedded JPEG output.
std::vector<uint8_t> applyRawBoost(const std::vector<uint8_t>& src, int pixelCount);

// Box-filter downsample of an RGB (3 bytes/pixel) buffer by an integer scale
// factor.  Averaging is performed in gamma-encoded space (same convention as
// the rest of the pipeline).  outW/outH receive the result dimensions.
std::vector<uint8_t> downsampleRgb(const uint8_t* src, int srcW, int srcH,
                                    int scale, int& outW, int& outH);

// Convert interleaved RGB (3 bytes/pixel) to RGBA (4 bytes/pixel, alpha=255).
std::vector<uint8_t> rgbToRgba(const std::vector<uint8_t>& rgb, int pixelCount);

// Apply tone adjustments (exposure, temperature, contrast, saturation) to an
// interleaved RGB buffer (3 bytes/pixel, w*h pixels).
//
// NOTE: all arithmetic runs in the gamma-encoded sRGB domain as received from
// LibRaw output_bps=8.  Exposure is therefore a straight pixel multiply rather
// than a linear-light multiply, which means:
//   - the visible brightening effect is stronger than one physical stop
//   - mid-gray (128) at +1 EV clips to 255 instead of the perceptually
//     expected ~174
// This is the intended behaviour; a gamma-correct path would require
// linearise → multiply → re-encode, which changes the look of all photos.
std::vector<uint8_t> applyAdjustments(const std::vector<uint8_t>& src,
                                       int w, int h,
                                       const catalog::EditSettings& s);

}  // namespace util
