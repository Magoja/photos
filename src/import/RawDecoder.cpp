#include "RawDecoder.h"
#include <libraw/libraw.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <memory>

namespace import_ns {

// ── EXIF helpers ──────────────────────────────────────────────────────────────
static std::string formatShutter(float v) {
    if (v <= 0.f) return "";
    if (v >= 1.f) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "%.0fs", v);
        return buf;
    }
    double denom = std::round(1.0 / v);
    char buf[32]; std::snprintf(buf, sizeof(buf), "1/%.0f", denom);
    return buf;
}

// "YYYY:MM:DD HH:MM:SS" → "YYYY-MM-DDTHH:MM:SS"
static std::string exifDateToIso(const char* src) {
    if (!src || !*src) return "";
    // expected format: "YYYY:MM:DD HH:MM:SS"
    char buf[20] = {};
    if (std::strlen(src) >= 19) {
        buf[0] = src[0]; buf[1] = src[1]; buf[2] = src[2]; buf[3] = src[3];
        buf[4] = '-';
        buf[5] = src[5]; buf[6] = src[6];
        buf[7] = '-';
        buf[8] = src[8]; buf[9] = src[9];
        buf[10] = 'T';
        buf[11] = src[11]; buf[12] = src[12];
        buf[13] = ':';
        buf[14] = src[14]; buf[15] = src[15];
        buf[16] = ':';
        buf[17] = src[17]; buf[18] = src[18];
    }
    return buf[0] ? std::string(buf) : "";
}

static double gpsToDecimal(const float pos[3], const char ref) {
    double deg = pos[0] + pos[1] / 60.0 + pos[2] / 3600.0;
    if (ref == 'S' || ref == 'W') deg = -deg;
    return deg;
}

// ── RawDecoder ────────────────────────────────────────────────────────────────
DecodeResult RawDecoder::decode(const std::string& filePath)
{
    DecodeResult result;
    auto raw = std::make_unique<LibRaw>();

    // Open
    int rc = raw->open_file(filePath.c_str());
    if (rc != LIBRAW_SUCCESS) {
        result.error = libraw_strerror(rc);
        spdlog::warn("RawDecoder: open_file({}) failed: {}", filePath, result.error);
        return result;
    }

    // ── EXIF ──────────────────────────────────────────────────────────────────
    const libraw_iparams_t&  ip   = raw->imgdata.idata;
    const libraw_imgother_t& io   = raw->imgdata.other;
    const libraw_image_sizes_t& is = raw->imgdata.sizes;
    const libraw_lensinfo_t& li  = raw->imgdata.lens;

    ExifData& ex = result.exif;
    ex.cameraMake   = ip.make;
    ex.cameraModel  = ip.model;
    ex.lensModel    = li.Lens;
    ex.captureTime  = exifDateToIso(io.timestamp > 0
        ? [&]() -> const char* {
            // libraw stores datetime as time_t in imgdata.other.timestamp
            static char buf[20];
            struct tm* t = gmtime(&io.timestamp);
            if (t) strftime(buf, sizeof(buf), "%Y:%m:%d %H:%M:%S", t);
            else buf[0] = '\0';
            return buf;
        }() : "");
    ex.focalLengthMm = io.focal_len;
    ex.aperture      = io.aperture;
    ex.shutterSpeed  = formatShutter(io.shutter);
    ex.iso           = static_cast<int>(io.iso_speed);
    ex.widthPx       = is.width  > 0 ? is.width  : is.raw_width;
    ex.heightPx      = is.height > 0 ? is.height : is.raw_height;

    // GPS
    const libraw_gps_info_t& gps = raw->imgdata.other.parsed_gps;
    if (gps.gpsparsed) {
        ex.gpsLat  = gpsToDecimal(gps.latitude,  gps.latref);
        ex.gpsLon  = gpsToDecimal(gps.longitude, gps.longref);
        ex.gpsAltM = gps.altitude;
    }

    // ── Thumbnail extraction ──────────────────────────────────────────────────
    rc = raw->unpack_thumb();
    if (rc == LIBRAW_SUCCESS) {
        libraw_processed_image_t* thumb = raw->dcraw_make_mem_thumb(&rc);
        if (thumb && rc == LIBRAW_SUCCESS) {
            if (thumb->type == LIBRAW_IMAGE_JPEG) {
                result.thumbJpeg.assign(thumb->data, thumb->data + thumb->data_size);
            }
            LibRaw::dcraw_clear_mem(thumb);
        }
    }

    // If no embedded JPEG thumbnail, try dcraw_process for a small preview
    if (result.thumbJpeg.empty()) {
        raw->imgdata.params.half_size       = 1;
        raw->imgdata.params.use_camera_wb   = 1;
        raw->imgdata.params.output_bps      = 8;
        if (raw->unpack() == LIBRAW_SUCCESS &&
            raw->dcraw_process() == LIBRAW_SUCCESS) {
            int prc = 0;
            libraw_processed_image_t* img = raw->dcraw_make_mem_image(&prc);
            if (img && prc == LIBRAW_SUCCESS) {
                // Convert raw RGB to minimal JPEG using libjpeg-turbo would be done
                // in ThumbnailCache; here just store the raw bytes isn't ideal,
                // but we'll leave thumbJpeg empty and let ThumbnailCache handle it.
                LibRaw::dcraw_clear_mem(img);
            }
        }
    }

    result.ok = true;
    return result;
}

} // namespace import_ns
