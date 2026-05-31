#include "ImageLoader.h"
#include "PixelPipeline.h"
#include <libraw/libraw.h>
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace util {

// ── LibRaw path ───────────────────────────────────────────────────────────────

static RgbImage decodeWithLibRaw(const std::string& path, int maxEdge) {
  auto raw = std::make_unique<LibRaw>();
  raw->imgdata.params.output_bps    = 8;
  raw->imgdata.params.use_camera_wb = 1;

  if (raw->open_file(path.c_str())  != LIBRAW_SUCCESS ||
      raw->unpack()                  != LIBRAW_SUCCESS ||
      raw->dcraw_process()           != LIBRAW_SUCCESS) {
    return {};
  }

  libraw_processed_image_t* img = raw->dcraw_make_mem_image();
  if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3) {
    if (img) { LibRaw::dcraw_clear_mem(img); }
    return {};
  }

  RgbImage result;
  if (maxEdge > 0 && std::max(img->width, img->height) > maxEdge) {
    const int scale = std::max(1, std::max(img->width, img->height) / maxEdge);
    result.pixels = downsampleRgb(img->data, img->width, img->height, scale,
                                  result.width, result.height);
  } else {
    result.pixels.assign(img->data, img->data + img->width * img->height * 3);
    result.width  = img->width;
    result.height = img->height;
  }
  LibRaw::dcraw_clear_mem(img);
  return result;
}

// ── libjpeg-turbo JPEG path ───────────────────────────────────────────────────

static RgbImage decodeWithTurboJpeg(const std::string& path, int maxEdge) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { return {}; }
  struct Guard { FILE* fp; ~Guard() { std::fclose(fp); } } g{f};

  uint8_t soi[2] = {};
  if (std::fread(soi, 1, 2, f) != 2 || soi[0] != 0xFF || soi[1] != 0xD8) { return {}; }
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz <= 0) { return {}; }

  std::vector<uint8_t> bytes(static_cast<size_t>(sz));
  if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) { return {}; }

  tjhandle tj = tjInitDecompress();
  if (!tj) { return {}; }
  struct TjGuard { tjhandle h; ~TjGuard() { tjDestroy(h); } } tg{tj};

  int srcW = 0, srcH = 0, subsamp = 0, cs = 0;
  if (tjDecompressHeader3(tj, bytes.data(), static_cast<unsigned long>(sz),
                          &srcW, &srcH, &subsamp, &cs) < 0) {
    return {};
  }

  std::vector<uint8_t> rgb(static_cast<size_t>(srcW * srcH * 3));
  if (tjDecompress2(tj, bytes.data(), static_cast<unsigned long>(sz),
                    rgb.data(), srcW, 0, srcH, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
    return {};
  }

  RgbImage result;
  if (maxEdge > 0 && std::max(srcW, srcH) > maxEdge) {
    const int scale = std::max(1, std::max(srcW, srcH) / maxEdge);
    result.pixels = downsampleRgb(rgb.data(), srcW, srcH, scale,
                                  result.width, result.height);
  } else {
    result.pixels = std::move(rgb);
    result.width  = srcW;
    result.height = srcH;
  }
  return result;
}

// ── Public API ────────────────────────────────────────────────────────────────

RgbImage loadImageAsRgb(const std::string& path, int maxEdge) {
  auto result = decodeWithLibRaw(path, maxEdge);
  if (result.ok()) { return result; }

  spdlog::debug("ImageLoader: LibRaw failed for '{}', trying JPEG fallback", path);
  result = decodeWithTurboJpeg(path, maxEdge);
  if (!result.ok()) {
    spdlog::warn("ImageLoader: could not decode '{}'", path);
  }
  return result;
}

}  // namespace util
