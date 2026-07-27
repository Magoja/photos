#pragma once
#include "catalog/EditSettings.h"
#include <vector>
#include <cstdint>

namespace util {

// Box-filter downsample of an RGB (3 bytes/pixel) buffer by an integer scale
// factor.  Averaging is performed in gamma-encoded space (same convention as
// the rest of the pipeline).  outW/outH receive the result dimensions.
std::vector<uint8_t> downsampleRgb(const uint8_t* src, int srcW, int srcH, int scale, int& outW,
                                   int& outH);

// Convert interleaved RGB (3 bytes/pixel) to RGBA (4 bytes/pixel, alpha=255).
std::vector<uint8_t> rgbToRgba(const std::vector<uint8_t>& rgb, int pixelCount);

// Convert interleaved RGBA (4 bytes/pixel) to RGB (3 bytes/pixel), dropping alpha.
std::vector<uint8_t> rgbaToRgb(const std::vector<uint8_t>& rgba, int pixelCount);

// BT.601 average luma of an interleaved RGB (3 bytes/pixel) buffer.
float computeLuma(const uint8_t* rgb, int pixelCount);

// Apply tone adjustments (exposure, temperature, contrast, saturation) to an
// interleaved RGB buffer (3 bytes/pixel, w*h pixels).
//
// Exposure and temperature (white balance) are light-domain gains, so they run
// in LINEAR light: each input byte is decoded sRGB→linear, scaled, hard-clipped
// to [0,1], then re-encoded sRGB→byte (via a precomputed per-channel LUT). This
// makes +1 EV a gentle one-stop lift (mid-gray 128 → ~177) instead of clipping
// to white. Contrast and saturation are perceptual "look" controls and remain
// in the gamma-encoded sRGB domain (contrast pivots around 128).
std::vector<uint8_t> applyAdjustments(const std::vector<uint8_t>& src, int w, int h,
                                      const catalog::EditSettings& s);

// Apply bilinear-interpolation rotation (in-place over crop buffer).
// angleDeg > 0 rotates clockwise. No-op when angleDeg == 0.
std::vector<uint8_t> rotateCropBuffer(const std::vector<uint8_t>& src, int w, int h,
                                      float angleDeg);

// Extract crop rectangle then apply straighten rotation.
// outW/outH receive result dimensions.
std::vector<uint8_t> cropAndRotatePixels(const std::vector<uint8_t>& src, int srcW, int srcH,
                                         const catalog::CropRect& crop, int& outW, int& outH);

// Bake the full non-destructive edit into a display-ready RGBA buffer: tone
// (applyAdjustments), then crop + straighten (cropAndRotatePixels), then add an
// opaque alpha channel. outW/outH receive the cropped dimensions. Used by
// FullscreenView so the on-screen texture is the finished image, matching
// EditView's preview and the Exporter.
std::vector<uint8_t> applyEditsToRgba(const std::vector<uint8_t>& rgb, int w, int h,
                                      const catalog::EditSettings& s, int& outW, int& outH);

// EXIF orientation values (tag 0x0112).
enum class Orientation : int {
  Normal = 1,
  Rotate180 = 3,
  Rotate90CW = 6,
  Rotate90CCW = 8,
};

// Parse JPEG bytes for the EXIF Orientation tag (0x0112).
// Returns Orientation::Normal if absent or unreadable.
Orientation readJpegOrientation(const uint8_t* data, size_t size);

// Rotate/flip an interleaved RGB (3 bytes/pixel) buffer to match the EXIF orientation.
// w and h are updated in place for 90°/270° rotations.
void applyOrientationRgb(std::vector<uint8_t>& rgb, int& w, int& h, Orientation orientation);

}  // namespace util
