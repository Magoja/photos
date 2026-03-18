#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/ThumbCropUV.h"

using namespace ui;
using Catch::Approx;

TEST_CASE("thumbCropUV: identity crop → full UV") {
  ThumbMeta m;
  const auto uv = thumbCropUV(m);
  REQUIRE(uv.u0 == 0.f);
  REQUIRE(uv.v0 == 0.f);
  REQUIRE(uv.u1 == 1.f);
  REQUIRE(uv.v1 == 1.f);
}

TEST_CASE("thumbCropUV: pre-cropped thumbnail → full UV ignoring crop values") {
  const ThumbMeta m{true, 0.1f, 0.2f, 0.5f, 0.4f};
  const auto uv = thumbCropUV(m);
  REQUIRE(uv.u0 == 0.f);
  REQUIRE(uv.v0 == 0.f);
  REQUIRE(uv.u1 == 1.f);
  REQUIRE(uv.v1 == 1.f);
}

TEST_CASE("thumbCropUV: camera JPEG with non-identity crop → crop UV") {
  const ThumbMeta m{false, 0.1f, 0.2f, 0.6f, 0.5f};
  const auto uv = thumbCropUV(m);
  REQUIRE(uv.u0 == Approx(0.1f));
  REQUIRE(uv.v0 == Approx(0.2f));
  REQUIRE(uv.u1 == Approx(0.7f));
  REQUIRE(uv.v1 == Approx(0.7f));
}

TEST_CASE("thumbCropUV: partial crop only in width — crops correctly") {
  // cropW < 1 while cropH == 1 → IS a crop (guard requires BOTH >= 1 for identity)
  const ThumbMeta m{false, 0.f, 0.f, 0.5f, 1.f};
  const auto uv = thumbCropUV(m);
  REQUIRE(uv.u0 == Approx(0.f));
  REQUIRE(uv.v0 == Approx(0.f));
  REQUIRE(uv.u1 == Approx(0.5f));
  REQUIRE(uv.v1 == Approx(1.f));
}
