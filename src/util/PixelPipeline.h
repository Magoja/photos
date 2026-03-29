#pragma once
#include "catalog/EditSettings.h"
#include <vector>
#include <cstdint>

namespace util {

// Box-filter downsample of an RGB (3 bytes/pixel) buffer by an integer scale
// factor.  Averaging is performed in gamma-encoded space (same convention as
// the rest of the pipeline).  outW/outH receive the result dimensions.
std::vector<uint8_t> downsampleRgb(const uint8_t* src, int srcW, int srcH,
                                    int scale, int& outW, int& outH);

// Convert interleaved RGB (3 bytes/pixel) to RGBA (4 bytes/pixel, alpha=255).
std::vector<uint8_t> rgbToRgba(const std::vector<uint8_t>& rgb, int pixelCount);

// BT.601 average luma of an interleaved RGB (3 bytes/pixel) buffer.
float computeLuma(const uint8_t* rgb, int pixelCount);

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

// Apply bilinear-interpolation rotation (in-place over crop buffer).
// angleDeg > 0 rotates clockwise. No-op when angleDeg == 0.
std::vector<uint8_t> rotateCropBuffer(const std::vector<uint8_t>& src,
                                      int w, int h, float angleDeg);

// Extract crop rectangle then apply straighten rotation.
// outW/outH receive result dimensions.
std::vector<uint8_t> cropAndRotatePixels(const std::vector<uint8_t>& src,
                                         int srcW, int srcH,
                                         const catalog::CropRect& crop,
                                         int& outW, int& outH);

}  // namespace util
