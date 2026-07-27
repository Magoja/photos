#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "util/PixelPipeline.h"

using namespace util;
using Catch::Approx;

// Convenience: apply to a single pixel given as initializer list.
static std::vector<uint8_t> apply1(std::vector<uint8_t> pixel, const catalog::EditSettings& s) {
  return applyAdjustments(pixel, 1, 1, s);
}

// ── Identity ──────────────────────────────────────────────────────────────────

TEST_CASE("applyAdjustments: zero settings are a perfect identity") {
  const catalog::EditSettings zero{};
  const std::vector<uint8_t> cases[] = {{0, 0, 0}, {255, 255, 255}, {100, 150, 200}, {50, 200, 80}};
  for (const auto& pix : cases) {
    REQUIRE(apply1(pix, zero) == pix);
  }
}

// ── Exposure ──────────────────────────────────────────────────────────────────

TEST_CASE("applyAdjustments: exposure +1 stop is a linear-light one-stop lift") {
  // Exposure is applied in linear light: decode sRGB→linear, ×2, re-encode.
  // The visible bytes therefore lift gently rather than simply doubling.
  catalog::EditSettings s{};
  s.exposure = 1.f;

  REQUIRE(apply1({50, 80, 100}, s)[0] == Approx(71).margin(1));
  REQUIRE(apply1({50, 80, 100}, s)[1] == Approx(111).margin(1));
  REQUIRE(apply1({50, 80, 100}, s)[2] == Approx(138).margin(1));
  // A fully-saturated channel still clips to white:
  REQUIRE(apply1({200, 200, 200}, s) == std::vector<uint8_t>{255, 255, 255});
}

TEST_CASE("applyAdjustments: exposure -1 stop is a linear-light one-stop drop") {
  catalog::EditSettings s{};
  s.exposure = -1.f;  // linear ×0.5

  REQUIRE(apply1({100, 160, 200}, s)[0] == Approx(71).margin(1));
  REQUIRE(apply1({100, 160, 200}, s)[1] == Approx(116).margin(1));
  REQUIRE(apply1({100, 160, 200}, s)[2] == Approx(146).margin(1));
  REQUIRE(apply1({0, 0, 0}, s) == std::vector<uint8_t>{0, 0, 0});
}

TEST_CASE("applyAdjustments: mid-gray at +1EV lifts gently and does NOT clip") {
  // Regression: gamma-space exposure used to send mid-gray (128) to 255 (pure
  // white), which read as blown highlights / excessive contrast. Linear-light
  // exposure lifts it to ~176 instead.
  catalog::EditSettings s{};
  s.exposure = 1.f;

  const auto out = apply1({128, 128, 128}, s);
  REQUIRE(out[0] == Approx(176).margin(1));
  REQUIRE(out[0] < 200);  // the key fix: no premature clip to white
  REQUIRE(out[1] == out[0]);
  REQUIRE(out[2] == out[0]);
}

// ── Contrast ──────────────────────────────────────────────────────────────────

TEST_CASE("applyAdjustments: contrast = 0 is identity") {
  catalog::EditSettings s{};
  s.contrast = 0.f;
  const std::vector<uint8_t> pix = {80, 128, 200};
  REQUIRE(apply1(pix, s) == pix);
}

TEST_CASE("applyAdjustments: contrast = 100 doubles range around mid-point 128") {
  // cFact = 1 + 100/100 = 2.0
  // output = 128 + (input - 128) * 2
  catalog::EditSettings s{};
  s.contrast = 100.f;

  // Mid-point stays at 128
  REQUIRE(apply1({128, 128, 128}, s) == std::vector<uint8_t>{128, 128, 128});
  // Below mid-point: 64 → 128 + (64-128)*2 = 0
  REQUIRE(apply1({64, 64, 64}, s) == std::vector<uint8_t>{0, 0, 0});
  // Above mid-point: 192 → 128 + (192-128)*2 = 256 → 255
  REQUIRE(apply1({192, 192, 192}, s) == std::vector<uint8_t>{255, 255, 255});
}

TEST_CASE("applyAdjustments: contrast = -100 collapses everything to mid-gray") {
  // cFact = 0 → output = 128 for every channel
  catalog::EditSettings s{};
  s.contrast = -100.f;

  REQUIRE(apply1({0, 0, 0}, s) == std::vector<uint8_t>{128, 128, 128});
  REQUIRE(apply1({255, 255, 255}, s) == std::vector<uint8_t>{128, 128, 128});
  REQUIRE(apply1({80, 160, 200}, s) == std::vector<uint8_t>{128, 128, 128});
}

// ── Saturation ────────────────────────────────────────────────────────────────

TEST_CASE("applyAdjustments: saturation = 0 is identity") {
  catalog::EditSettings s{};
  s.saturation = 0.f;
  const std::vector<uint8_t> pix = {200, 100, 50};
  REQUIRE(apply1(pix, s) == pix);
}

TEST_CASE("applyAdjustments: saturation = -100 converts to BT.601 luma") {
  // sFact = 0 → all channels become L = 0.299R + 0.587G + 0.114B
  catalog::EditSettings s{};
  s.saturation = -100.f;

  // L = 0.299*200 + 0.587*100 + 0.114*50 = 59.8 + 58.7 + 5.7 = 124.2 → 124
  const auto out = apply1({200, 100, 50}, s);
  REQUIRE(out[0] == 124);
  REQUIRE(out[1] == 124);
  REQUIRE(out[2] == 124);
}

// ── Temperature ───────────────────────────────────────────────────────────────

TEST_CASE("applyAdjustments: temperature = 0 is identity") {
  catalog::EditSettings s{};
  s.temperature = 0.f;
  const std::vector<uint8_t> pix = {100, 150, 200};
  REQUIRE(apply1(pix, s) == pix);
}

TEST_CASE("applyAdjustments: positive temperature warms (more red, less blue)") {
  // t = 100/100 = 1.0 → linear gains rMul=1.30, gMul=1.05, bMul=0.70, applied in
  // linear light then re-encoded. The warming direction (r > g > b) is preserved.
  catalog::EditSettings s{};
  s.temperature = 100.f;

  const auto out = apply1({100, 100, 100}, s);
  REQUIRE(out[0] == Approx(113).margin(1));  // red lifted
  REQUIRE(out[1] == Approx(102).margin(1));  // green ~unchanged
  REQUIRE(out[2] == Approx(84).margin(1));   // blue lowered
  REQUIRE(out[0] > out[1]);
  REQUIRE(out[1] > out[2]);
}

// ── downsampleRgb ─────────────────────────────────────────────────────────────

TEST_CASE("downsampleRgb: scale=1 is identity") {
  const std::vector<uint8_t> src = {10, 20, 30, 40, 50, 60};
  int outW = 0, outH = 0;
  const auto out = util::downsampleRgb(src.data(), 2, 1, 1, outW, outH);
  REQUIRE(outW == 2);
  REQUIRE(outH == 1);
  REQUIRE(out == src);
}

TEST_CASE("downsampleRgb: scale=2 averages 2x2 block") {
  // R: 100+200+50+150=500/4=125; G: 0+0+0+0=0; B: 200+200+200+200=200
  const std::vector<uint8_t> src = {
    100, 0, 200, 200, 0, 200, 50, 0, 200, 150, 0, 200,
  };
  int outW = 0, outH = 0;
  const auto out = util::downsampleRgb(src.data(), 2, 2, 2, outW, outH);
  REQUIRE(outW == 1);
  REQUIRE(outH == 1);
  REQUIRE(out[0] == 125);
  REQUIRE(out[1] == 0);
  REQUIRE(out[2] == 200);
}

TEST_CASE("downsampleRgb: scale=2 preserves uniform color exactly") {
  const std::vector<uint8_t> src(4 * 4 * 3, 128);  // 4×4 mid-gray
  int outW = 0, outH = 0;
  const auto out = util::downsampleRgb(src.data(), 4, 4, 2, outW, outH);
  REQUIRE(outW == 2);
  REQUIRE(outH == 2);
  REQUIRE(std::all_of(out.begin(), out.end(), [](uint8_t v) { return v == 128; }));
}

// ── rgbToRgba ─────────────────────────────────────────────────────────────────

TEST_CASE("rgbToRgba: alpha channel is always 255") {
  const std::vector<uint8_t> rgb = {10, 20, 30, 40, 50, 60};
  const auto rgba = util::rgbToRgba(rgb, 2);
  REQUIRE(rgba.size() == 8u);
  REQUIRE(rgba[3] == 255);  // first pixel alpha
  REQUIRE(rgba[7] == 255);  // second pixel alpha
}

TEST_CASE("rgbToRgba: RGB channels are copied verbatim") {
  const std::vector<uint8_t> rgb = {1, 2, 3, 4, 5, 6};
  const auto rgba = util::rgbToRgba(rgb, 2);
  REQUIRE(rgba[0] == 1);
  REQUIRE(rgba[1] == 2);
  REQUIRE(rgba[2] == 3);
  REQUIRE(rgba[4] == 4);
  REQUIRE(rgba[5] == 5);
  REQUIRE(rgba[6] == 6);
}

TEST_CASE("rgbToRgba: zero pixels → empty output") {
  const std::vector<uint8_t> rgb;
  REQUIRE(util::rgbToRgba(rgb, 0).empty());
}

// ── computeLuma ───────────────────────────────────────────────────────────────

TEST_CASE("computeLuma: empty (0 pixels) returns 0") {
  REQUIRE(util::computeLuma(nullptr, 0) == Approx(0.f));
}

TEST_CASE("computeLuma: black image returns 0") {
  const std::vector<uint8_t> rgb(9, 0);  // 3 pixels, all black
  REQUIRE(util::computeLuma(rgb.data(), 3) == Approx(0.f));
}

TEST_CASE("computeLuma: white image returns 255") {
  const std::vector<uint8_t> rgb(9, 255);  // 3 pixels, all white
  REQUIRE(util::computeLuma(rgb.data(), 3) == Approx(255.f).epsilon(0.01));
}

TEST_CASE("computeLuma: BT.601 weights — pure red gives 0.299*255") {
  // Single pixel: R=255, G=0, B=0 → luma = 0.299 * 255 ≈ 76.245
  const std::vector<uint8_t> rgb = {255, 0, 0};
  REQUIRE(util::computeLuma(rgb.data(), 1) == Approx(0.299f * 255.f).epsilon(0.01));
}

// ── Pipeline order (exposure before contrast) ─────────────────────────────────

TEST_CASE("applyAdjustments: exposure (linear) applied before contrast (gamma)") {
  // exposure = 1 lifts 80 to ~111 in linear light, THEN contrast = 100 (cFact=2)
  // pivots around 128 in gamma space: 128 + (111 - 128) * 2 ≈ 95.
  catalog::EditSettings s{};
  s.exposure = 1.f;
  s.contrast = 100.f;

  const auto out = apply1({80, 80, 80}, s);
  REQUIRE(out[0] == Approx(95).margin(1));
  REQUIRE(out[1] == out[0]);
  REQUIRE(out[2] == out[0]);
}

// ── cropAndRotatePixels ───────────────────────────────────────────────────────

TEST_CASE("cropAndRotatePixels: identity crop is a no-op") {
  // 3×2 RGB image with distinct per-pixel values.
  const std::vector<uint8_t> src = {10, 10, 10, 20, 20, 20, 30, 30, 30,
                                    40, 40, 40, 50, 50, 50, 60, 60, 60};
  catalog::CropRect crop{};  // x=0,y=0,w=1,h=1,angleDeg=0
  int outW = 0, outH = 0;
  const auto out = cropAndRotatePixels(src, 3, 2, crop, outW, outH);
  REQUIRE(outW == 3);
  REQUIRE(outH == 2);
  REQUIRE(out == src);
}

TEST_CASE("cropAndRotatePixels: right-half crop extracts the correct columns") {
  // 4×1 grayscale ramp: columns 0..3 = 0,10,20,30.
  const std::vector<uint8_t> src = {0, 0, 0, 10, 10, 10, 20, 20, 20, 30, 30, 30};
  catalog::CropRect crop{};
  crop.x = 0.5f;
  crop.w = 0.5f;  // → cropX=2, outW=2
  int outW = 0, outH = 0;
  const auto out = cropAndRotatePixels(src, 4, 1, crop, outW, outH);
  REQUIRE(outW == 2);
  REQUIRE(outH == 1);
  REQUIRE(out == std::vector<uint8_t>{20, 20, 20, 30, 30, 30});
}

TEST_CASE("cropAndRotatePixels: top-left quadrant crop") {
  // 2×2: (0,0)=10 (1,0)=20 (0,1)=30 (1,1)=40.
  const std::vector<uint8_t> src = {10, 10, 10, 20, 20, 20, 30, 30, 30, 40, 40, 40};
  catalog::CropRect crop{};
  crop.w = 0.5f;
  crop.h = 0.5f;  // → outW=1, outH=1, pixel (0,0)
  int outW = 0, outH = 0;
  const auto out = cropAndRotatePixels(src, 2, 2, crop, outW, outH);
  REQUIRE(outW == 1);
  REQUIRE(outH == 1);
  REQUIRE(out == std::vector<uint8_t>{10, 10, 10});
}

TEST_CASE("cropAndRotatePixels: straighten keeps dimensions; uniform field stays uniform") {
  const std::vector<uint8_t> src(5 * 5 * 3, 100);  // 5×5 solid gray
  catalog::CropRect crop{};
  crop.angleDeg = 10.f;  // rotate-then-crop path
  int outW = 0, outH = 0;
  const auto out = cropAndRotatePixels(src, 5, 5, crop, outW, outH);
  REQUIRE(outW == 5);
  REQUIRE(outH == 5);
  // Bilinear sampling of a uniform field returns ~100 everywhere (the rotate path
  // truncates to int, so allow a 1-LSB rounding tolerance).
  REQUIRE(std::all_of(out.begin(), out.end(), [](uint8_t v) { return v >= 99 && v <= 100; }));
}

// ── applyEditsToRgba (the FullscreenView baking path) ─────────────────────────

TEST_CASE("applyEditsToRgba: identity edit → source RGB with opaque alpha") {
  const std::vector<uint8_t> rgb = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};  // 2×2
  const catalog::EditSettings s{};
  int outW = 0, outH = 0;
  const auto out = util::applyEditsToRgba(rgb, 2, 2, s, outW, outH);
  REQUIRE(outW == 2);
  REQUIRE(outH == 2);
  REQUIRE(out == util::rgbToRgba(rgb, 4));
}

TEST_CASE("applyEditsToRgba: crop is applied to the baked output") {
  // Regression guard: FullscreenView must bake the crop into the texture, not
  // ignore it. 4×1 ramp, crop the right half → 2×1 RGBA of columns 2,3.
  const std::vector<uint8_t> rgb = {0, 0, 0, 10, 10, 10, 20, 20, 20, 30, 30, 30};
  catalog::EditSettings s{};
  s.crop.x = 0.5f;
  s.crop.w = 0.5f;
  int outW = 0, outH = 0;
  const auto out = util::applyEditsToRgba(rgb, 4, 1, s, outW, outH);
  REQUIRE(outW == 2);
  REQUIRE(outH == 1);
  REQUIRE(out == std::vector<uint8_t>{20, 20, 20, 255, 30, 30, 30, 255});
}

TEST_CASE("applyEditsToRgba: tone (exposure) is applied to the baked output") {
  // Mid-gray + exposure +1 lifts to ~176 (linear light), alpha stays opaque.
  const std::vector<uint8_t> rgb = {128, 128, 128};
  catalog::EditSettings s{};
  s.exposure = 1.f;
  int outW = 0, outH = 0;
  const auto out = util::applyEditsToRgba(rgb, 1, 1, s, outW, outH);
  REQUIRE(outW == 1);
  REQUIRE(outH == 1);
  REQUIRE(out[0] == Approx(176).margin(2));
  REQUIRE(out[0] > 128);
  REQUIRE(out[3] == 255);
}
