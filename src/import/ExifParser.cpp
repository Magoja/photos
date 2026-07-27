#include "ExifParser.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace import_ns {

namespace {

// Format an EXIF ExposureTime (seconds) as a shutter string: "1/250" or "2s".
static std::string formatExposure(double seconds) {
  if (seconds <= 0.0) {
    return "";
  }
  char buf[32];
  if (seconds >= 1.0) {
    std::snprintf(buf, sizeof(buf), "%.0fs", seconds);
  } else {
    std::snprintf(buf, sizeof(buf), "1/%.0f", std::round(1.0 / seconds));
  }
  return buf;
}

// ── TIFF primitive readers ────────────────────────────────────────────────────
//
// All offsets are relative to the start of the TIFF block (the bytes following
// the "Exif\0\0" APP1 header). Every read is bounds-checked against the span.

struct TiffCtx {
  std::span<const uint8_t> tiff;
  bool bigEndian = false;
};

static uint16_t readU16(const TiffCtx& c, size_t off) {
  if (off + 2 > c.tiff.size()) {
    return 0;
  }
  const uint8_t a = c.tiff[off];
  const uint8_t b = c.tiff[off + 1];
  return c.bigEndian ? static_cast<uint16_t>(a << 8 | b) : static_cast<uint16_t>(b << 8 | a);
}

static uint32_t readU32(const TiffCtx& c, size_t off) {
  if (off + 4 > c.tiff.size()) {
    return 0;
  }
  const uint32_t a = c.tiff[off], b = c.tiff[off + 1], d = c.tiff[off + 2], e = c.tiff[off + 3];
  return c.bigEndian ? (a << 24 | b << 16 | d << 8 | e) : (e << 24 | d << 16 | b << 8 | a);
}

// A single RATIONAL = two U32 (numerator / denominator). Returns 0 on zero denom.
static double readRational(const TiffCtx& c, size_t off) {
  const uint32_t num = readU32(c, off);
  const uint32_t den = readU32(c, off + 4);
  if (den == 0) {
    return 0.0;
  }
  return static_cast<double>(num) / static_cast<double>(den);
}

// ── IFD entry lookup ──────────────────────────────────────────────────────────

enum class TiffType : uint16_t {
  Byte = 1,
  Ascii = 2,
  Short = 3,
  Long = 4,
  Rational = 5,
};

static size_t typeSize(uint16_t type) {
  switch (static_cast<TiffType>(type)) {
    case TiffType::Byte:
    case TiffType::Ascii:
      return 1;
    case TiffType::Short:
      return 2;
    case TiffType::Long:
      return 4;
    case TiffType::Rational:
      return 8;
    default:
      return 0;
  }
}

struct IfdEntry {
  uint16_t type = 0;
  uint32_t count = 0;
  size_t valueOff = 0;  // resolved offset into the TIFF block of the first value
};

// Find the entry with `wantTag` in the IFD starting at `ifdOff`. Resolves the
// value location: inline (bytes 8..11 of the entry) when the payload fits in
// 4 bytes, otherwise the offset those bytes encode.
static std::optional<IfdEntry> findEntry(const TiffCtx& c, size_t ifdOff, uint16_t wantTag) {
  const uint16_t count = readU16(c, ifdOff);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t e = ifdOff + 2 + static_cast<size_t>(i) * 12;
    if (e + 12 > c.tiff.size()) {
      break;
    }
    if (readU16(c, e) != wantTag) {
      continue;
    }
    const uint16_t type = readU16(c, e + 2);
    const uint32_t cnt = readU32(c, e + 4);
    const size_t total = typeSize(type) * cnt;
    const size_t valueOff = (total <= 4) ? (e + 8) : readU32(c, e + 8);
    return IfdEntry{.type = type, .count = cnt, .valueOff = valueOff};
  }
  return std::nullopt;
}

// ── Typed accessors ───────────────────────────────────────────────────────────

static std::string readAscii(const TiffCtx& c, const IfdEntry& e) {
  if (e.type != static_cast<uint16_t>(TiffType::Ascii) || e.count == 0) {
    return "";
  }
  const size_t len = e.count - 1;  // exclude trailing null
  if (e.valueOff + len > c.tiff.size()) {
    return "";
  }
  const char* p = reinterpret_cast<const char*>(c.tiff.data() + e.valueOff);
  return std::string(p, ::strnlen(p, len));
}

static std::optional<double> asciiTagRational(const TiffCtx& c, size_t ifdOff, uint16_t tag) {
  const auto e = findEntry(c, ifdOff, tag);
  if (!e || e->type != static_cast<uint16_t>(TiffType::Rational) || e->count == 0) {
    return std::nullopt;
  }
  return readRational(c, e->valueOff);
}

static std::optional<uint32_t> readShortOrLong(const TiffCtx& c, size_t ifdOff, uint16_t tag) {
  const auto e = findEntry(c, ifdOff, tag);
  if (!e || e->count == 0) {
    return std::nullopt;
  }
  if (e->type == static_cast<uint16_t>(TiffType::Short)) {
    return readU16(c, e->valueOff);
  }
  if (e->type == static_cast<uint16_t>(TiffType::Long)) {
    return readU32(c, e->valueOff);
  }
  return std::nullopt;
}

// ── Date formatting ───────────────────────────────────────────────────────────

// "YYYY:MM:DD HH:MM:SS" → "YYYY-MM-DDTHH:MM:SS"; returns "" on malformed input.
static std::string exifDateToIso(const std::string& src) {
  if (src.size() < 19) {
    return "";
  }
  std::string out = src.substr(0, 19);
  out[4] = '-';
  out[7] = '-';
  out[10] = 'T';
  out[13] = ':';
  out[16] = ':';
  return out;
}

// ── GPS ───────────────────────────────────────────────────────────────────────

// Decode a GPSLatitude/GPSLongitude RATIONAL[3] (deg, min, sec) at `off`.
static double gpsDmsToDecimal(const TiffCtx& c, size_t off, char ref) {
  const double deg = readRational(c, off);
  const double min = readRational(c, off + 8);
  const double sec = readRational(c, off + 16);
  double value = deg + min / 60.0 + sec / 3600.0;
  if (ref == 'S' || ref == 'W') {
    value = -value;
  }
  return value;
}

static char firstChar(const std::string& s) {
  return s.empty() ? '\0' : s[0];
}

static void parseGpsIfd(const TiffCtx& c, size_t gpsIfd, ExifData& ex) {
  const auto lat = findEntry(c, gpsIfd, 0x0002);
  const auto lon = findEntry(c, gpsIfd, 0x0004);
  if (lat && lat->count >= 3) {
    const auto ref = findEntry(c, gpsIfd, 0x0001);
    ex.gpsLat = gpsDmsToDecimal(c, lat->valueOff, ref ? firstChar(readAscii(c, *ref)) : 'N');
  }
  if (lon && lon->count >= 3) {
    const auto ref = findEntry(c, gpsIfd, 0x0003);
    ex.gpsLon = gpsDmsToDecimal(c, lon->valueOff, ref ? firstChar(readAscii(c, *ref)) : 'E');
  }
  if (const auto alt = asciiTagRational(c, gpsIfd, 0x0006)) {
    const auto refEntry = findEntry(c, gpsIfd, 0x0005);
    const bool belowSea = refEntry && readU16(c, refEntry->valueOff) == 1;
    ex.gpsAltM = belowSea ? -*alt : *alt;
  }
}

// ── ExifIFD (sub-IFD holding capture time, exposure, lens) ─────────────────────

static void parseExifSubIfd(const TiffCtx& c, size_t exifIfd, ExifData& ex) {
  const auto dtOrig = findEntry(c, exifIfd, 0x9003);  // DateTimeOriginal
  const auto dtDig = findEntry(c, exifIfd, 0x9004);   // DateTimeDigitized
  if (dtOrig) {
    ex.captureTime = exifDateToIso(readAscii(c, *dtOrig));
  }
  if (ex.captureTime.empty() && dtDig) {
    ex.captureTime = exifDateToIso(readAscii(c, *dtDig));
  }
  if (const auto lens = findEntry(c, exifIfd, 0xA434)) {
    ex.lensModel = readAscii(c, *lens);
  }
  if (const auto iso = readShortOrLong(c, exifIfd, 0x8827)) {
    ex.iso = static_cast<int>(*iso);
  }
  if (const auto fnum = asciiTagRational(c, exifIfd, 0x829D)) {
    ex.aperture = *fnum;
  }
  if (const auto focal = asciiTagRational(c, exifIfd, 0x920A)) {
    ex.focalLengthMm = *focal;
  }
  if (const auto exp = asciiTagRational(c, exifIfd, 0x829A)) {
    ex.shutterSpeed = formatExposure(*exp);
  }
  if (const auto w = readShortOrLong(c, exifIfd, 0xA002)) {
    ex.widthPx = static_cast<int>(*w);
  }
  if (const auto h = readShortOrLong(c, exifIfd, 0xA003)) {
    ex.heightPx = static_cast<int>(*h);
  }
}

// ── APP1 discovery ────────────────────────────────────────────────────────────

// Walk JPEG markers for the APP1 "Exif\0\0" segment; return its TIFF sub-span.
static std::optional<std::span<const uint8_t>> findExifTiff(std::span<const uint8_t> jpeg) {
  if (jpeg.size() < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
    return std::nullopt;  // not a JPEG SOI
  }
  size_t pos = 2;
  while (pos + 4 <= jpeg.size()) {
    if (jpeg[pos] != 0xFF) {
      break;
    }
    const uint8_t marker = jpeg[pos + 1];
    if (marker == 0xD9 || marker == 0xDA) {
      break;  // EOI or start-of-scan: no metadata beyond here
    }
    const uint16_t seglen = static_cast<uint16_t>(jpeg[pos + 2] << 8 | jpeg[pos + 3]);
    if (seglen < 2) {
      break;
    }
    const size_t dataStart = pos + 4;
    const size_t dataLen = static_cast<size_t>(seglen) - 2;
    if (dataStart + dataLen > jpeg.size()) {
      break;
    }
    if (marker == 0xE1 && dataLen >= 6 &&
        std::memcmp(jpeg.data() + dataStart, "Exif\0\0", 6) == 0) {
      return jpeg.subspan(dataStart + 6, dataLen - 6);
    }
    pos = dataStart + dataLen;
  }
  return std::nullopt;
}

}  // namespace

std::optional<ExifData> parseJpegExif(std::span<const uint8_t> fileBytes) {
  const auto tiffSpan = findExifTiff(fileBytes);
  if (!tiffSpan || tiffSpan->size() < 8) {
    return std::nullopt;
  }
  const auto tiff = *tiffSpan;
  const bool bigEndian = (tiff[0] == 0x4D && tiff[1] == 0x4D);
  const bool littleEndian = (tiff[0] == 0x49 && tiff[1] == 0x49);
  if (!bigEndian && !littleEndian) {
    return std::nullopt;
  }
  const TiffCtx c{.tiff = tiff, .bigEndian = bigEndian};
  if (readU16(c, 2) != 42) {
    return std::nullopt;  // bad TIFF magic
  }
  const uint32_t ifd0 = readU32(c, 4);

  ExifData ex;
  if (const auto make = findEntry(c, ifd0, 0x010F)) {
    ex.cameraMake = readAscii(c, *make);
  }
  if (const auto model = findEntry(c, ifd0, 0x0110)) {
    ex.cameraModel = readAscii(c, *model);
  }
  if (const auto exifPtr = findEntry(c, ifd0, 0x8769)) {
    parseExifSubIfd(c, readU32(c, exifPtr->valueOff), ex);
  }
  if (ex.captureTime.empty()) {
    if (const auto dt = findEntry(c, ifd0, 0x9003)) {  // DateTimeOriginal in IFD0
      ex.captureTime = exifDateToIso(readAscii(c, *dt));
    }
  }
  if (const auto gpsPtr = findEntry(c, ifd0, 0x8825)) {
    parseGpsIfd(c, readU32(c, gpsPtr->valueOff), ex);
  }
  return ex;
}

}  // namespace import_ns
