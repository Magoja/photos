#include "ImageDecoder.h"
#include "FileScanner.h"
#include "util/PixelPipeline.h"
#include <libraw/libraw.h>
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <memory>
#include <optional>

namespace import_ns {

namespace {

std::optional<DecodedImage> decodeJpeg(const std::string& srcPath) {
  std::ifstream f(srcPath, std::ios::binary);
  if (!f) {
    spdlog::warn("ImageDecoder: cannot read JPEG {}", srcPath);
    return std::nullopt;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});

  tjhandle tj = tjInitDecompress();
  if (!tj) {
    return std::nullopt;
  }
  int w = 0, h = 0, ss = 0, cs = 0;
  if (tjDecompressHeader3(tj, bytes.data(), static_cast<unsigned long>(bytes.size()), &w, &h, &ss,
                          &cs) < 0) {
    tjDestroy(tj);
    spdlog::warn("ImageDecoder: JPEG header read failed for {}", srcPath);
    return std::nullopt;
  }
  std::vector<uint8_t> rgb(static_cast<size_t>(w * h) * 3);
  if (tjDecompress2(tj, bytes.data(), static_cast<unsigned long>(bytes.size()), rgb.data(), w, 0,
                    h, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
    tjDestroy(tj);
    spdlog::warn("ImageDecoder: JPEG decompress failed for {}", srcPath);
    return std::nullopt;
  }
  tjDestroy(tj);

  const util::Orientation orientation = util::readJpegOrientation(bytes.data(), bytes.size());
  util::applyOrientationRgb(rgb, w, h, orientation);

  return DecodedImage{.rgb = std::move(rgb), .w = w, .h = h};
}

std::optional<DecodedImage> decodeRaw(const std::string& srcPath) {
  auto raw = std::make_unique<LibRaw>();
  if (raw->open_file(srcPath.c_str()) != LIBRAW_SUCCESS) {
    spdlog::warn("ImageDecoder: LibRaw open failed for {}", srcPath);
    return std::nullopt;
  }
  if (raw->unpack() != LIBRAW_SUCCESS) {
    spdlog::warn("ImageDecoder: LibRaw unpack failed for {}", srcPath);
    return std::nullopt;
  }
  raw->imgdata.params.output_bps = 8;
  raw->imgdata.params.use_camera_wb = 1;
  if (raw->dcraw_process() != LIBRAW_SUCCESS) {
    spdlog::warn("ImageDecoder: LibRaw dcraw_process failed for {}", srcPath);
    return std::nullopt;
  }

  libraw_processed_image_t* img = raw->dcraw_make_mem_image();
  if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3) {
    if (img) {
      LibRaw::dcraw_clear_mem(img);
    }
    spdlog::warn("ImageDecoder: LibRaw image format unexpected for {}", srcPath);
    return std::nullopt;
  }

  const int w = img->width;
  const int h = img->height;
  std::vector<uint8_t> rgb(img->data, img->data + static_cast<size_t>(w * h * 3));
  LibRaw::dcraw_clear_mem(img);

  return DecodedImage{.rgb = std::move(rgb), .w = w, .h = h};
}

}  // namespace

std::optional<DecodedImage> decodeSource(const std::string& srcPath) {
  if (FileScanner::isJpeg(srcPath)) {
    return decodeJpeg(srcPath);
  }
  return decodeRaw(srcPath);
}

}  // namespace import_ns
