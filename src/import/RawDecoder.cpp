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
  if (!tj) { return -1.f; }
  int srcW = 0, srcH = 0, subsamp = 0;
  if (tjDecompressHeader2(tj, const_cast<unsigned char*>(jpeg.data()),
                          static_cast<unsigned long>(jpeg.size()),
                          &srcW, &srcH, &subsamp) < 0) {
    tjDestroy(tj);
    return -1.f;
  }
  const int dstW = std::max(1, srcW / 8);
  const int dstH = std::max(1, srcH / 8);
  std::vector<uint8_t> rgb(static_cast<size_t>(dstW * dstH) * 3);
  tjDecompress2(tj, const_cast<unsigned char*>(jpeg.data()),
                static_cast<unsigned long>(jpeg.size()),
                rgb.data(), dstW, 0, dstH, TJPF_RGB, TJFLAG_FASTDCT);
  tjDestroy(tj);
  return util::computeLuma(rgb.data(), dstW * dstH);
}

// Do a half-size LibRaw decode on an already-open (but not yet unpacked) raw instance.
// Returns the luminance scale (LibRaw luma / jpeg luma), clamped to [0.25, 1.0].
// Returns 1.0 on any failure.
static float computeRawLumaScale(LibRaw& raw, const std::vector<uint8_t>& thumbJpeg) {
  if (raw.imgdata.idata.raw_count == 0) { return 1.f; }

  const float jpegLuma = computeJpegLuma(thumbJpeg);
  if (jpegLuma < 1.f) { return 1.f; }

  raw.imgdata.params.half_size     = 1;
  raw.imgdata.params.output_bps    = 8;
  raw.imgdata.params.use_camera_wb = 1;
  if (raw.unpack()        != LIBRAW_SUCCESS) { return 1.f; }
  if (raw.dcraw_process() != LIBRAW_SUCCESS) { return 1.f; }

  libraw_processed_image_t* img = raw.dcraw_make_mem_image();
  if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3) {
    if (img) { LibRaw::dcraw_clear_mem(img); }
    return 1.f;
  }
  const int pixelCount = img->width * img->height;
  const float rawLuma = util::computeLuma(img->data, pixelCount);
  LibRaw::dcraw_clear_mem(img);

  if (rawLuma < 0.5f) { return 1.f; }
  return std::clamp(rawLuma / jpegLuma, 0.25f, 1.0f);
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

// ── RawDecoder ────────────────────────────────────────────────────────────────

DecodeResult RawDecoder::decode(const std::string& filePath) {
  DecodeResult result;
  auto raw = std::make_unique<LibRaw>();

  int rc = raw->open_file(filePath.c_str());
  if (rc != LIBRAW_SUCCESS) {
    result.error = libraw_strerror(rc);
    spdlog::warn("RawDecoder: open_file({}) failed: {}", filePath, result.error);
    return result;
  }

  extractExif(*raw, result.exif);
  extractThumbnail(*raw, result);

  if (!result.thumbJpeg.empty()) {
    result.lumaScale = computeRawLumaScale(*raw, result.thumbJpeg);
  }

  result.ok = true;
  return result;
}

}  // namespace import_ns
