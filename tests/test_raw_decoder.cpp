#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "import/RawDecoder.h"
#include "catalog/ThumbnailCache.h"
#include "util/PixelPipeline.h"
#include <turbojpeg.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <filesystem>

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

// ── JPEG EXIF fallback tests ──────────────────────────────────────────────────

namespace {

// Encodes a 1×1 white JPEG using libjpeg-turbo.
static std::vector<uint8_t> makeTinyJpeg() {
  tjhandle tj = tjInitCompress();
  REQUIRE(tj);
  uint8_t pixel[3] = {255, 255, 255};
  unsigned char* buf = nullptr;
  unsigned long  sz  = 0;
  REQUIRE(tjCompress2(tj, pixel, 1, 0, 1, TJPF_RGB, &buf, &sz, TJSAMP_444, 90, 0) == 0);
  std::vector<uint8_t> out(buf, buf + sz);
  tjFree(buf);
  tjDestroy(tj);
  return out;
}

// Builds a TIFF (little-endian) block containing a single DateTimeOriginal (0x9003) tag.
static std::vector<uint8_t> makeTiffWithDate(const std::string& exifDate) {
  const std::string dt = exifDate.size() >= 19 ? exifDate.substr(0, 19) : "2000:01:01 00:00:00";
  // Layout: header(8) + IFD count(2) + 1 entry(12) + next-IFD(4) + date string(20)
  std::vector<uint8_t> tiff(46, 0);
  tiff[0] = 'I'; tiff[1] = 'I';
  tiff[2] = 42;  tiff[3] = 0;               // magic
  tiff[4] = 8;                               // IFD0 at offset 8
  tiff[8] = 1;                               // 1 entry
  tiff[10] = 0x03; tiff[11] = 0x90;         // tag 0x9003
  tiff[12] = 0x02;                           // type ASCII
  tiff[14] = 20;                             // count (includes null terminator)
  tiff[18] = 26;                             // value at offset 26 from TIFF start
  for (int i = 0; i < 19; ++i) { tiff[26 + i] = static_cast<uint8_t>(dt[i]); }
  return tiff;
}

// Builds a JPEG file with a single APP1 EXIF segment containing the given date.
static std::vector<uint8_t> makeJpegWithExif(const std::string& exifDate) {
  const auto tiff = makeTiffWithDate(exifDate);
  const auto base = makeTinyJpeg();

  const auto segDataLen = static_cast<uint16_t>(6 + tiff.size());
  const auto segLen     = static_cast<uint16_t>(2 + segDataLen);

  std::vector<uint8_t> out;
  out.push_back(0xFF); out.push_back(0xD8);        // SOI
  out.push_back(0xFF); out.push_back(0xE1);        // APP1 marker
  out.push_back(static_cast<uint8_t>(segLen >> 8));
  out.push_back(static_cast<uint8_t>(segLen & 0xFF));
  const char* hdr = "Exif\0\0";
  for (int i = 0; i < 6; ++i) { out.push_back(static_cast<uint8_t>(hdr[i])); }
  out.insert(out.end(), tiff.begin(), tiff.end());
  out.insert(out.end(), base.begin() + 2, base.end()); // skip original SOI
  return out;
}

static std::string writeTempFile(const std::string& name, const std::vector<uint8_t>& data) {
  const auto path = (std::filesystem::temp_directory_path() / name).string();
  FILE* f = std::fopen(path.c_str(), "wb");
  REQUIRE(f);
  std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
  return path;
}

}  // namespace

TEST_CASE("RawDecoder: JPEG with EXIF DateTimeOriginal reads correct captureTime and thumbnail") {
  const auto path = writeTempFile("test_exif.jpg", makeJpegWithExif("2026:05:30 12:34:56"));
  struct Cleanup { std::string p; ~Cleanup() { std::remove(p.c_str()); } } _{path};

  const auto res = import_ns::RawDecoder::decode(path);
  CHECK(res.ok);
  CHECK(!res.thumbJpeg.empty());
  CHECK(res.exif.captureTime == "2026-05-30T12:34:56");
}

TEST_CASE("RawDecoder: JPEG without EXIF has thumbnail and falls back to mtime") {
  const auto path = writeTempFile("test_no_exif.jpg", makeTinyJpeg());
  struct Cleanup { std::string p; ~Cleanup() { std::remove(p.c_str()); } } _{path};

  const auto res = import_ns::RawDecoder::decode(path);
  CHECK(res.ok);
  CHECK(!res.thumbJpeg.empty());
  REQUIRE(!res.exif.captureTime.empty());
  // "YYYY-MM-DDTHH:MM:SS"
  CHECK(res.exif.captureTime.size() == 19);
  CHECK(res.exif.captureTime[4]  == '-');
  CHECK(res.exif.captureTime[7]  == '-');
  CHECK(res.exif.captureTime[10] == 'T');
}
