#include "RawDecoder.h"
#include "util/PixelPipeline.h"
#include <libraw/libraw.h>
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <memory>
#include <chrono>
#include <filesystem>
#include <span>
#include <vector>

namespace import_ns {

// ── EXIF formatting helpers ───────────────────────────────────────────────────

static std::string formatShutter(float v) {
  if (v <= 0.f) {
    return "";
  }
  if (v >= 1.f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0fs", v);
    return buf;
  }
  double denom = std::round(1.0 / v);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "1/%.0f", denom);
  return buf;
}

static std::string timestampToExifString(time_t ts) {
  char buf[20] = {};
  struct tm* t = gmtime(&ts);
  if (t) {
    strftime(buf, sizeof(buf), "%Y:%m:%d %H:%M:%S", t);
  }
  return buf;
}

// "YYYY:MM:DD HH:MM:SS" → "YYYY-MM-DDTHH:MM:SS"
static std::string exifDateToIso(const char* src) {
  if (!src || !*src) {
    return "";
  }
  char buf[20] = {};
  if (std::strlen(src) >= 19) {
    buf[0] = src[0];
    buf[1] = src[1];
    buf[2] = src[2];
    buf[3] = src[3];
    buf[4] = '-';
    buf[5] = src[5];
    buf[6] = src[6];
    buf[7] = '-';
    buf[8] = src[8];
    buf[9] = src[9];
    buf[10] = 'T';
    buf[11] = src[11];
    buf[12] = src[12];
    buf[13] = ':';
    buf[14] = src[14];
    buf[15] = src[15];
    buf[16] = ':';
    buf[17] = src[17];
    buf[18] = src[18];
  }
  return buf[0] ? std::string(buf) : "";
}

static double gpsToDecimal(const float pos[3], const char ref) {
  double deg = pos[0] + pos[1] / 60.0 + pos[2] / 3600.0;
  if (ref == 'S' || ref == 'W') {
    deg = -deg;
  }
  return deg;
}

// ── Luma scale helpers ────────────────────────────────────────────────────────

// Decode jpeg at 1/8 size; return BT.601 average luma, or -1 on error.
static float computeJpegLuma(const std::vector<uint8_t>& jpeg) {
  tjhandle tj = tjInitDecompress();
  if (!tj) {
    return -1.f;
  }
  int srcW = 0, srcH = 0, subsamp = 0;
  if (tjDecompressHeader2(tj, const_cast<unsigned char*>(jpeg.data()),
                          static_cast<unsigned long>(jpeg.size()), &srcW, &srcH, &subsamp) < 0) {
    tjDestroy(tj);
    return -1.f;
  }
  const int dstW = std::max(1, srcW / 8);
  const int dstH = std::max(1, srcH / 8);
  std::vector<uint8_t> rgb(static_cast<size_t>(dstW * dstH) * 3);
  tjDecompress2(tj, const_cast<unsigned char*>(jpeg.data()),
                static_cast<unsigned long>(jpeg.size()), rgb.data(), dstW, 0, dstH, TJPF_RGB,
                TJFLAG_FASTDCT);
  tjDestroy(tj);
  return util::computeLuma(rgb.data(), dstW * dstH);
}

// Do a half-size LibRaw decode on an already-open (but not yet unpacked) raw instance.
// Returns the luminance scale (LibRaw luma / jpeg luma), clamped to [0.25, 1.0].
// Returns 1.0 on any failure (JPEG-only file, degenerate image, etc.).
static float computeRawLumaScale(LibRaw& raw, const std::vector<uint8_t>& thumbJpeg,
                                 const std::string& filePath) {
  const float jpegLuma = computeJpegLuma(thumbJpeg);
  if (jpegLuma < 1.f) {
    spdlog::debug("lumaScale({}): jpegLuma={:.1f} — skipping (too dark or error)", filePath,
                  jpegLuma);
    return 1.f;
  }

  raw.imgdata.params.half_size = 1;
  raw.imgdata.params.output_bps = 8;
  raw.imgdata.params.use_camera_wb = 1;

  const int rc1 = raw.unpack();
  if (rc1 != LIBRAW_SUCCESS) {
    spdlog::debug("lumaScale({}): unpack() failed ({}), raw_count={} — JPEG-only or unsupported",
                  filePath, libraw_strerror(rc1), raw.imgdata.idata.raw_count);
    return 1.f;
  }
  const int rc2 = raw.dcraw_process();
  if (rc2 != LIBRAW_SUCCESS) {
    spdlog::debug("lumaScale({}): dcraw_process() failed ({})", filePath, libraw_strerror(rc2));
    return 1.f;
  }

  libraw_processed_image_t* img = raw.dcraw_make_mem_image();
  if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3) {
    if (img) {
      LibRaw::dcraw_clear_mem(img);
    }
    spdlog::debug("lumaScale({}): dcraw_make_mem_image returned unexpected type", filePath);
    return 1.f;
  }
  const int pixelCount = img->width * img->height;
  const float rawLuma = util::computeLuma(img->data, pixelCount);
  LibRaw::dcraw_clear_mem(img);

  if (rawLuma < 0.5f) {
    spdlog::debug("lumaScale({}): rawLuma={:.1f} too dark — skipping", filePath, rawLuma);
    return 1.f;
  }

  const float scale = std::clamp(rawLuma / jpegLuma, 0.25f, 1.0f);
  spdlog::debug("lumaScale({}): jpegLuma={:.1f} rawLuma={:.1f} → scale={:.3f}", filePath, jpegLuma,
                rawLuma, scale);
  return scale;
}

// ── JPEG EXIF fallback reader ─────────────────────────────────────────────────
//
// LibRaw populates io.timestamp reliably for RAW formats but often returns 0 for
// plain JPEG files.  These helpers parse the JPEG APP1 EXIF segment directly.

namespace fs = std::filesystem;

static uint16_t tiffU16(std::span<const uint8_t> d, size_t off, bool be) {
  if (off + 2 > d.size()) {
    return 0;
  }
  return be ? static_cast<uint16_t>(d[off] << 8 | d[off + 1])
            : static_cast<uint16_t>(d[off + 1] << 8 | d[off]);
}

static uint32_t tiffU32(std::span<const uint8_t> d, size_t off, bool be) {
  if (off + 4 > d.size()) {
    return 0;
  }
  return be ? (uint32_t(d[off]) << 24 | uint32_t(d[off + 1]) << 16 | uint32_t(d[off + 2]) << 8 |
               d[off + 3])
            : (uint32_t(d[off + 3]) << 24 | uint32_t(d[off + 2]) << 16 | uint32_t(d[off + 1]) << 8 |
               d[off]);
}

// Searches `ifd` (offset from tiff start) for an ASCII tag. Returns value or "".
static std::string tiffAsciiTag(std::span<const uint8_t> tiff, uint32_t ifdOff, bool be,
                                uint16_t wantTag) {
  if (ifdOff + 2 > tiff.size()) {
    return "";
  }
  const uint16_t count = tiffU16(tiff, ifdOff, be);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t e = ifdOff + 2 + size_t(i) * 12;
    if (e + 12 > tiff.size()) {
      break;
    }
    const uint16_t tag = tiffU16(tiff, e, be);
    const uint16_t type = tiffU16(tiff, e + 2, be);
    const uint32_t cnt = tiffU32(tiff, e + 4, be);
    if (tag != wantTag || type != 2 || cnt == 0) {
      continue;
    }
    const size_t len = cnt - 1;  // exclude null terminator
    if (cnt <= 4) {
      return std::string(reinterpret_cast<const char*>(tiff.data() + e + 8), len);
    }
    const uint32_t valOff = tiffU32(tiff, e + 8, be);
    if (valOff + cnt > tiff.size()) {
      return "";
    }
    return std::string(reinterpret_cast<const char*>(tiff.data() + valOff), len);
  }
  return "";
}

// Returns DateTimeOriginal (or fallbacks) from raw TIFF bytes, in EXIF string format.
static std::string parseTiffDate(std::span<const uint8_t> tiff) {
  if (tiff.size() < 8) {
    return "";
  }
  const bool be = (tiff[0] == 0x4D && tiff[1] == 0x4D);
  if (tiffU16(tiff, 2, be) != 42) {
    return "";
  }
  const uint32_t ifd0 = tiffU32(tiff, 4, be);

  // Check ExifIFD sub-IFD (tag 0x8769) for DateTimeOriginal (0x9003).
  const uint16_t count = tiffU16(tiff, ifd0, be);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t e = ifd0 + 2 + size_t(i) * 12;
    if (e + 12 > tiff.size()) {
      break;
    }
    if (tiffU16(tiff, e, be) == 0x8769) {
      const uint32_t exifIfd = tiffU32(tiff, e + 8, be);
      const auto dt = tiffAsciiTag(tiff, exifIfd, be, 0x9003);
      if (!dt.empty()) {
        return dt;
      }
      const auto dtd = tiffAsciiTag(tiff, exifIfd, be, 0x9004);
      if (!dtd.empty()) {
        return dtd;
      }
      break;
    }
  }

  // Fall back to DateTimeOriginal or DateTime in main IFD.
  const auto dt0 = tiffAsciiTag(tiff, ifd0, be, 0x9003);
  if (!dt0.empty()) {
    return dt0;
  }
  return tiffAsciiTag(tiff, ifd0, be, 0x0132);
}

// Parses JPEG markers to find the APP1 EXIF segment and extract the date string.
// Returns "YYYY:MM:DD HH:MM:SS" on success, "" on failure.
static std::string readJpegExifDateString(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return "";
  }
  struct Guard {
    FILE* fp;
    ~Guard() { std::fclose(fp); }
  } g{f};

  uint8_t soi[2];
  if (std::fread(soi, 1, 2, f) != 2 || soi[0] != 0xFF || soi[1] != 0xD8) {
    return "";
  }

  while (true) {
    uint8_t hdr[4];
    if (std::fread(hdr, 1, 4, f) != 4 || hdr[0] != 0xFF) {
      break;
    }
    const uint8_t marker = hdr[1];
    const uint16_t seglen = static_cast<uint16_t>(hdr[2] << 8 | hdr[3]);
    if (seglen < 2) {
      break;
    }
    const uint16_t datalen = seglen - 2;
    if (marker == 0xD9 || marker == 0xDA) {
      break;
    }

    if (marker == 0xE1 && datalen >= 6) {
      uint8_t exifHdr[6];
      if (std::fread(exifHdr, 1, 6, f) != 6) {
        break;
      }
      const uint16_t rem = datalen - 6;
      if (std::memcmp(exifHdr, "Exif\0\0", 6) == 0 && rem >= 8) {
        std::vector<uint8_t> tiff(rem);
        if (std::fread(tiff.data(), 1, rem, f) != rem) {
          break;
        }
        return parseTiffDate(tiff);
      }
      if (std::fseek(f, rem, SEEK_CUR) != 0) {
        break;
      }
    } else {
      if (std::fseek(f, datalen, SEEK_CUR) != 0) {
        break;
      }
    }
  }
  return "";
}

// Returns ISO 8601 capture time derived from the file's modification time.
static std::string filemtimeToIso(const std::string& path) {
  std::error_code ec;
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return "";
  }
  const auto sctp = std::chrono::file_clock::to_sys(mtime);
  const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(sctp);
  const std::time_t tt = static_cast<std::time_t>(secs.time_since_epoch().count());
  char buf[20] = {};
  if (const struct tm* t = gmtime(&tt)) {
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", t);
  }
  return buf;
}

// ── EXIF + GPS extraction ─────────────────────────────────────────────────────

static void extractExif(LibRaw& raw, ExifData& ex) {
  const auto& ip = raw.imgdata.idata;
  const auto& io = raw.imgdata.other;
  const auto& is = raw.imgdata.sizes;
  const auto& li = raw.imgdata.lens;

  ex.cameraMake = ip.make;
  ex.cameraModel = ip.model;
  ex.lensModel = li.Lens;
  ex.focalLengthMm = io.focal_len;
  ex.aperture = io.aperture;
  ex.shutterSpeed = formatShutter(io.shutter);
  ex.iso = static_cast<int>(io.iso_speed);
  ex.widthPx = is.width > 0 ? is.width : is.raw_width;
  ex.heightPx = is.height > 0 ? is.height : is.raw_height;

  std::string tsStr = io.timestamp > 0 ? timestampToExifString(io.timestamp) : "";
  ex.captureTime = exifDateToIso(tsStr.c_str());

  const auto& gps = raw.imgdata.other.parsed_gps;
  if (gps.gpsparsed) {
    ex.gpsLat = gpsToDecimal(gps.latitude, gps.latref);
    ex.gpsLon = gpsToDecimal(gps.longitude, gps.longref);
    ex.gpsAltM = gps.altitude;
  }
}

static void extractThumbnail(LibRaw& raw, DecodeResult& result) {
  int rc = raw.unpack_thumb();
  if (rc != LIBRAW_SUCCESS) {
    return;
  }

  libraw_processed_image_t* thumb = raw.dcraw_make_mem_thumb(&rc);
  if (thumb && rc == LIBRAW_SUCCESS) {
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
      result.thumbJpeg.assign(thumb->data, thumb->data + thumb->data_size);
    }
    LibRaw::dcraw_clear_mem(thumb);
  }
}

// Reads the raw bytes of a file only if it begins with a JPEG SOI marker (FF D8).
// Used to populate thumbJpeg for plain JPEG inputs that LibRaw cannot open.
static std::vector<uint8_t> readJpegFileBytes(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return {};
  }
  struct Guard {
    FILE* fp;
    ~Guard() { std::fclose(fp); }
  } g{f};
  uint8_t soi[2] = {};
  if (std::fread(soi, 1, 2, f) != 2 || soi[0] != 0xFF || soi[1] != 0xD8) {
    return {};
  }
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    return {};
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(sz));
  std::fread(bytes.data(), 1, bytes.size(), f);
  return bytes;
}

// Fill widthPx/heightPx from a JPEG's SOF header — the authoritative encoded size.
static void setJpegDimensions(const std::vector<uint8_t>& jpeg, ExifData& ex) {
  tjhandle tj = tjInitDecompress();
  if (!tj) {
    return;
  }
  int w = 0, h = 0, ss = 0, cs = 0;
  if (tjDecompressHeader3(tj, jpeg.data(), static_cast<unsigned long>(jpeg.size()), &w, &h, &ss,
                          &cs) >= 0) {
    ex.widthPx = w;
    ex.heightPx = h;
  }
  tjDestroy(tj);
}

// Populate EXIF from a plain JPEG's bytes (make/model/GPS/date/exposure + size).
static void fillJpegExif(const std::vector<uint8_t>& jpeg, ExifData& exif) {
  if (auto parsed = parseJpegExif(jpeg)) {
    exif = *parsed;  // make/model/GPS/date/exposure from the JPEG's EXIF
  }
  setJpegDimensions(jpeg, exif);  // authoritative encoded size
}

// Ensure captureTime is set, falling back to the JPEG APP1 date, then file mtime.
static void fillCaptureTimeFallback(const std::string& filePath, ExifData& exif) {
  if (exif.captureTime.empty()) {
    exif.captureTime = exifDateToIso(readJpegExifDateString(filePath).c_str());
  }
  if (exif.captureTime.empty()) {
    exif.captureTime = filemtimeToIso(filePath);
  }
}

// ── RawDecoder ────────────────────────────────────────────────────────────────

DecodeResult RawDecoder::decode(const std::string& filePath) {
  DecodeResult result;
  auto raw = std::make_unique<LibRaw>();

  int rc = raw->open_file(filePath.c_str());
  if (rc != LIBRAW_SUCCESS) {
    result.error = libraw_strerror(rc);
    spdlog::warn("RawDecoder: open_file({}) failed: {}", filePath, result.error);
    result.thumbJpeg = readJpegFileBytes(filePath);
    if (!result.thumbJpeg.empty()) {
      result.ok = true;
      fillJpegExif(result.thumbJpeg, result.exif);
    }
    fillCaptureTimeFallback(filePath, result.exif);
    return result;
  }

  extractExif(*raw, result.exif);
  fillCaptureTimeFallback(filePath, result.exif);
  extractThumbnail(*raw, result);

  if (!result.thumbJpeg.empty()) {
    result.lumaScale = computeRawLumaScale(*raw, result.thumbJpeg, filePath);
  }

  result.ok = true;
  return result;
}

ExifData RawDecoder::decodeMetadata(const std::string& filePath) {
  // Metadata-only: skips thumbnail extraction and luma-scale (the expensive
  // LibRaw unpack/dcraw_process steps) — used to backfill EXIF/GPS on existing
  // catalog rows without re-decoding pixels.
  ExifData exif;
  auto raw = std::make_unique<LibRaw>();

  const int rc = raw->open_file(filePath.c_str());
  if (rc != LIBRAW_SUCCESS) {
    if (const auto jpeg = readJpegFileBytes(filePath); !jpeg.empty()) {
      fillJpegExif(jpeg, exif);
    }
    fillCaptureTimeFallback(filePath, exif);
    return exif;
  }

  extractExif(*raw, exif);
  fillCaptureTimeFallback(filePath, exif);
  return exif;
}

}  // namespace import_ns
