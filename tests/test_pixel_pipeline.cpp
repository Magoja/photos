#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "util/PixelPipeline.h"

using namespace util;
using Catch::Approx;

// Convenience: apply to a single pixel given as initializer list.
static std::vector<uint8_t> apply1(std::vector<uint8_t> pixel,
                                   const catalog::EditSettings& s) {
  return applyAdjustments(pixel, 1, 1, s);
}

// ── Identity ──────────────────────────────────────────────────────────────────

TEST_CASE("applyAdjustments: zero settings are a perfect identity") {
  const catalog::EditSettings zero{};
  const std::vector<uint8_t> cases[] = {
    {0, 0, 0}, {255, 255, 255}, {100, 150, 200}, {50, 200, 80}
  };
  for (const auto& pix : cases) {
    REQUIRE(apply1(pix, zero) == pix);
  }
}

// ── Exposure ──────────────────────────────────────────────────────────────────

TEST_CASE("applyAdjustments: exposure +1 stop doubles pixel values (gamma-space)") {
  // eMul = 2^1 = 2.  With all other settings at zero/identity,
  // each channel is simply multiplied by 2 then clamped.
  catalog::EditSettings s{};
  s.exposure = 1.f;

  REQUIRE(apply1({50, 80, 100}, s) == std::vector<uint8_t>{100, 160, 200});
  // Channel that would exceed 255 is clamped:
  REQUIRE(apply1({200, 200, 200}, s) == std::vector<uint8_t>{255, 255, 255});
}

TEST_CASE("applyAdjustments: exposure -1 stop halves pixel values") {
  catalog::EditSettings s{};
  s.exposure = -1.f;  // eMul = 0.5

  REQUIRE(apply1({100, 160, 200}, s) == std::vector<uint8_t>{50, 80, 100});
  REQUIRE(apply1({0, 0, 0}, s)       == std::vector<uint8_t>{0, 0, 0});
}

TEST_CASE("applyAdjustments: exposure gamma-space brightness issue — mid-gray clips at +1EV") {
  // BUG DOCUMENTATION: arithmetic is in gamma-encoded space.
  // Mid-gray (128) at +1 stop gives 256 → clamped to 255 (white).
  // A gamma-correct (linear-light) +1 stop would give ≈174.
  // This test documents the current (gamma-space) behaviour so any change is
  // immediately visible.
  catalog::EditSettings s{};
  s.exposure = 1.f;

  const auto out = apply1({128, 128, 128}, s);
  // Current behaviour: gamma-space multiply → clips to white
  REQUIRE(out[0] == 255);
  REQUIRE(out[1] == 255);
  REQUIRE(out[2] == 255);
  // NOTE: gamma-correct behaviour would be ~174. If this test starts failing,
  // it means the pipeline was changed to linear-light exposure, which is
  // intentional but will alter the look of all exported photos.
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

  REQUIRE(apply1({0, 0, 0}, s)       == std::vector<uint8_t>{128, 128, 128});
  REQUIRE(apply1({255, 255, 255}, s) == std::vector<uint8_t>{128, 128, 128});
  REQUIRE(apply1({80, 160, 200}, s)  == std::vector<uint8_t>{128, 128, 128});
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
  // t = 100/100 = 1.0 → rMul=1.30, gMul=1.05, bMul=0.70
  catalog::EditSettings s{};
  s.temperature = 100.f;

  const auto out = apply1({100, 100, 100}, s);
  // r = 100*1.30 = 130; contrast neutral (128+(130-128)*1=130); sat neutral → 130
  // g = 100*1.05 = 105; → 105
  // b = 100*0.70 = 70;  → 70
  REQUIRE(out[0] == 130);
  REQUIRE(out[1] == 105);
  REQUIRE(out[2] == 70);
}

// ── Pipeline order (exposure before contrast) ─────────────────────────────────

TEST_CASE("applyAdjustments: exposure then contrast applied in order") {
  // exposure = 1 (eMul=2) → pixel 80 becomes 160
  // contrast = 100 (cFact=2) → 128 + (160-128)*2 = 192
  catalog::EditSettings s{};
  s.exposure  = 1.f;
  s.contrast  = 100.f;

  const auto out = apply1({80, 80, 80}, s);
  REQUIRE(out[0] == 192);
  REQUIRE(out[1] == 192);
  REQUIRE(out[2] == 192);
}
