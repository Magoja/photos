#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "catalog/PhotoRepository.h"
#include "export/ExifWriter.h"
#include "import/ExifParser.h"

#include <cstdint>
#include <string>
#include <vector>

using Catch::Approx;
using import_ns::parseJpegExif;

// ── helpers ──────────────────────────────────────────────────────────────────

// A minimal but structurally valid JPEG shell (SOI + EOI) to inject EXIF into.
static std::vector<uint8_t> minimalJpeg() {
  return {0xFF, 0xD8, 0xFF, 0xD9};
}

// Build a JPEG carrying `rec`'s metadata via the production export EXIF writer.
static std::vector<uint8_t> jpegWithExifFrom(const catalog::PhotoRecord& rec) {
  const auto payload = export_ns::buildExifPayload(rec);
  return export_ns::injectExifApp1(minimalJpeg(), payload);
}

static void putU16LE(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
static void putU32LE(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

// Wrap a raw TIFF block in an APP1 "Exif\0\0" segment inside a JPEG shell.
static std::vector<uint8_t> wrapTiffInJpeg(const std::vector<uint8_t>& tiff) {
  std::vector<uint8_t> payload = {'E', 'x', 'i', 'f', 0, 0};
  payload.insert(payload.end(), tiff.begin(), tiff.end());
  const uint16_t app1Len = static_cast<uint16_t>(2 + payload.size());
  std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE1,
                               static_cast<uint8_t>(app1Len >> 8),
                               static_cast<uint8_t>(app1Len & 0xFF)};
  jpeg.insert(jpeg.end(), payload.begin(), payload.end());
  jpeg.push_back(0xFF);
  jpeg.push_back(0xD9);
  return jpeg;
}

// ── round-trip through the export writer (proves import↔export parity) ─────────

TEST_CASE("parseJpegExif round-trips GPS, camera, and date from the export writer") {
  catalog::PhotoRecord rec;
  rec.cameraMake = "Canon";
  rec.cameraModel = "EOS R5";
  rec.captureTime = "2026-02-19T10:30:00";
  rec.gpsLat = 37.7749;
  rec.gpsLon = -122.4194;  // negative → West hemisphere
  rec.gpsAltM = 52.0;

  const auto ex = parseJpegExif(jpegWithExifFrom(rec));
  REQUIRE(ex.has_value());
  CHECK(ex->cameraMake == "Canon");
  CHECK(ex->cameraModel == "EOS R5");
  CHECK(ex->captureTime == "2026-02-19T10:30:00");
  CHECK(ex->gpsLat == Approx(37.7749).margin(1e-3));
  CHECK(ex->gpsLon == Approx(-122.4194).margin(1e-3));
  CHECK(ex->gpsAltM == Approx(52.0).margin(0.5));
}

TEST_CASE("exported JPEGs carry a Software tag identifying this tool") {
  catalog::PhotoRecord rec;
  rec.captureTime = "2026-02-19T10:30:00";
  // No camera/GPS: Software must be present regardless of other fields.

  const auto ex = parseJpegExif(jpegWithExifFrom(rec));
  REQUIRE(ex.has_value());
  CHECK(ex->software == "Jakeutil Photos Export");
}

TEST_CASE("parseJpegExif recovers southern/eastern hemisphere signs") {
  catalog::PhotoRecord rec;
  rec.captureTime = "2026-01-01T00:00:00";
  rec.gpsLat = -33.8688;  // Sydney: South
  rec.gpsLon = 151.2093;  // East

  const auto ex = parseJpegExif(jpegWithExifFrom(rec));
  REQUIRE(ex.has_value());
  CHECK(ex->gpsLat == Approx(-33.8688).margin(1e-3));
  CHECK(ex->gpsLon == Approx(151.2093).margin(1e-3));
}

TEST_CASE("parseJpegExif leaves GPS zero when the record has none") {
  catalog::PhotoRecord rec;
  rec.cameraMake = "Fujifilm";
  rec.captureTime = "2026-03-03T12:00:00";
  // no GPS set

  const auto ex = parseJpegExif(jpegWithExifFrom(rec));
  REQUIRE(ex.has_value());
  CHECK(ex->cameraMake == "Fujifilm");
  CHECK(ex->gpsLat == 0.0);
  CHECK(ex->gpsLon == 0.0);
}

// ── independent parsing (fields/endianness the writer does not exercise) ───────

TEST_CASE("parseJpegExif reads a little-endian ExifIFD ISO value") {
  // TIFF: IFD0 → ExifIFD pointer → ISO (SHORT, inline). No heap needed.
  std::vector<uint8_t> tiff = {'I', 'I'};
  putU16LE(tiff, 0x002A);
  putU32LE(tiff, 8);  // IFD0 at offset 8

  // IFD0 @8: one entry (ExifIFD pointer), ExifIFD lands at 8 + (2+12+4) = 26.
  putU16LE(tiff, 1);
  putU16LE(tiff, 0x8769);  // ExifIFD tag
  putU16LE(tiff, 4);       // LONG
  putU32LE(tiff, 1);
  putU32LE(tiff, 26);
  putU32LE(tiff, 0);  // next-IFD

  // ExifIFD @26: one entry ISO=100.
  putU16LE(tiff, 1);
  putU16LE(tiff, 0x8827);  // ISO tag
  putU16LE(tiff, 3);       // SHORT
  putU32LE(tiff, 1);
  putU32LE(tiff, 100);  // inline value (SHORT in low bytes)
  putU32LE(tiff, 0);    // next-IFD

  const auto ex = parseJpegExif(wrapTiffInJpeg(tiff));
  REQUIRE(ex.has_value());
  CHECK(ex->iso == 100);
}

TEST_CASE("parseJpegExif reads a big-endian Make tag") {
  // MM byte order, IFD0 with an inline ASCII Make = "AB".
  std::vector<uint8_t> tiff = {'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08};
  // IFD0 @8: count=1
  tiff.insert(tiff.end(), {0x00, 0x01});
  // Make: tag 0x010F, type 2 (ASCII), count 3, inline "AB\0"
  tiff.insert(tiff.end(), {0x01, 0x0F, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 'A', 'B', 0x00, 0x00});
  tiff.insert(tiff.end(), {0x00, 0x00, 0x00, 0x00});  // next-IFD

  const auto ex = parseJpegExif(wrapTiffInJpeg(tiff));
  REQUIRE(ex.has_value());
  CHECK(ex->cameraMake == "AB");
}

// ── malformed / missing input (must not crash, returns nullopt) ────────────────

TEST_CASE("parseJpegExif returns nullopt for non-JPEG input") {
  const std::vector<uint8_t> notJpeg = {0x00, 0x01, 0x02, 0x03};
  CHECK_FALSE(parseJpegExif(notJpeg).has_value());
  CHECK_FALSE(parseJpegExif({}).has_value());
}

TEST_CASE("parseJpegExif returns nullopt for a JPEG without EXIF") {
  const std::vector<uint8_t> bare = {0xFF, 0xD8, 0xFF, 0xD9};
  CHECK_FALSE(parseJpegExif(bare).has_value());
}

TEST_CASE("parseJpegExif returns nullopt for a truncated APP1 segment") {
  // Declares a long APP1 but the data runs off the end of the buffer.
  const std::vector<uint8_t> truncated = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x40,
                                          'E',  'x',  'i',  'f',  0x00, 0x00};
  CHECK_FALSE(parseJpegExif(truncated).has_value());
}

TEST_CASE("parseJpegExif returns nullopt for a bogus TIFF header") {
  const std::vector<uint8_t> tiff = {'X', 'X', 0x00, 0x00, 0x00, 0x00, 0x00, 0x08};
  CHECK_FALSE(parseJpegExif(wrapTiffInJpeg(tiff)).has_value());
}
