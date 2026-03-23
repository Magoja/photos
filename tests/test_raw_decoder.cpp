#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "import/RawDecoder.h"
#include "catalog/ThumbnailCache.h"
#include "util/PixelPipeline.h"
#include <turbojpeg.h>
#include <cstdint>
#include <vector>

using Catch::Approx;

// Path to the test RAW file, relative to the project root.
// ctest runs from the build directory, so use an absolute path baked in at build time.
static const char* kCR2 = TEST_DATA_DIR "/20260219/5F1A2661.CR2";

// ── helpers ──────────────────────────────────────────────────────────────────

static float jpegAverageLuma(const std::vector<uint8_t>& jpeg) {
  tjhandle tj = tjInitDecompress();
  REQUIRE(tj);
  int w = 0, h = 0, ss = 0, cs = 0;
  REQUIRE(tjDecompressHeader3(tj, jpeg.data(), (unsigned long)jpeg.size(),
                               &w, &h, &ss, &cs) >= 0);
  std::vector<uint8_t> rgb((size_t)w * h * 3);
  REQUIRE(tjDecompress2(tj, jpeg.data(), (unsigned long)jpeg.size(),
                         rgb.data(), w, 0, h, TJPF_RGB, TJFLAG_FASTDCT) >= 0);
  tjDestroy(tj);
  return util::computeLuma(rgb.data(), w * h);
}

// ── RawDecoder::decode tests ──────────────────────────────────────────────────

TEST_CASE("RawDecoder::decode: 5F1A2661.CR2 succeeds and has embedded JPEG") {
  const auto res = import_ns::RawDecoder::decode(kCR2);
  REQUIRE(res.ok);
  REQUIRE(!res.thumbJpeg.empty());
}

TEST_CASE("RawDecoder::decode: lumaScale < 1 for camera-JPEG (ISP tone curve is brighter than LibRaw)") {
  const auto res = import_ns::RawDecoder::decode(kCR2);
  REQUIRE(res.ok);
  // Scale must be a real correction (not the 1.0 fallback) and within clamp range.
  CHECK(res.lumaScale >= 0.25f);
  CHECK(res.lumaScale <  1.00f);
}

TEST_CASE("RawDecoder::decode: lumaScale is reproducible across two decode calls") {
  const auto a = import_ns::RawDecoder::decode(kCR2);
  const auto b = import_ns::RawDecoder::decode(kCR2);
  REQUIRE(a.ok);
  REQUIRE(b.ok);
  CHECK(a.lumaScale == Approx(b.lumaScale).epsilon(0.001f));
}

// ── resizeJpeg scale application ─────────────────────────────────────────────

TEST_CASE("resizeJpeg: scale < 1 darkens the output luma relative to unscaled") {
  const auto res = import_ns::RawDecoder::decode(kCR2);
  REQUIRE(res.ok);
  REQUIRE(!res.thumbJpeg.empty());

  const float scale = res.lumaScale;
  REQUIRE(scale < 1.f);

  const auto unscaled = catalog::ThumbnailCache::resizeJpeg(res.thumbJpeg,
                                                             catalog::ThumbnailCache::kMaxDim);
  const auto scaled   = catalog::ThumbnailCache::resizeJpeg(res.thumbJpeg,
                                                             catalog::ThumbnailCache::kMaxDim,
                                                             scale);

  REQUIRE(!unscaled.empty());
  REQUIRE(!scaled.empty());

  const float lumaUnscaled = jpegAverageLuma(unscaled);
  const float lumaScaled   = jpegAverageLuma(scaled);

  // Scaled output must be measurably darker.
  CHECK(lumaScaled < lumaUnscaled * 0.99f);
  // Must not be pathologically dark (scale >= 0.25 so at most 4x darker).
  CHECK(lumaScaled > lumaUnscaled * 0.20f);
}

TEST_CASE("resizeJpeg: scale=1.0 produces identical luma to unscaled path") {
  const auto res = import_ns::RawDecoder::decode(kCR2);
  REQUIRE(res.ok);
  REQUIRE(!res.thumbJpeg.empty());

  const auto a = catalog::ThumbnailCache::resizeJpeg(res.thumbJpeg,
                                                      catalog::ThumbnailCache::kMaxDim, 1.0f);
  const auto b = catalog::ThumbnailCache::resizeJpeg(res.thumbJpeg,
                                                      catalog::ThumbnailCache::kMaxDim);
  REQUIRE(!a.empty());
  REQUIRE(!b.empty());

  // Luma should be effectively identical (JPEG re-encode may introduce tiny rounding).
  CHECK(jpegAverageLuma(a) == Approx(jpegAverageLuma(b)).epsilon(0.01f));
}
