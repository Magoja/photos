#include "ThumbnailCache.h"
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace fs = std::filesystem;

namespace catalog {

ThumbnailCache::ThumbnailCache(const std::string& cacheRoot)
  : root_(cacheRoot), microRoot_(cacheRoot + "_micro") {
  fs::create_directories(root_);
  fs::create_directories(microRoot_);
}

std::string ThumbnailCache::pathFor(const std::string& hash) const {
  if (hash.size() < 2) {
    return root_ + "/00/" + hash + ".jpg";
  }
  return root_ + "/" + hash.substr(0, 2) + "/" + hash + ".jpg";
}

std::string ThumbnailCache::microPathFor(const std::string& hash) const {
  if (hash.size() < 2) {
    return microRoot_ + "/00/" + hash + ".jpg";
  }
  return microRoot_ + "/" + hash.substr(0, 2) + "/" + hash + ".jpg";
}

std::string ThumbnailCache::lookup(const std::string& hash) const {
  auto p = pathFor(hash);
  return fs::exists(p) ? p : "";
}

// ── libjpeg-turbo helpers ─────────────────────────────────────────────────────

static std::pair<int, int> scaleDimensions(int w, int h, int maxDim) {
  if (w <= maxDim && h <= maxDim) {
    return {w, h};
  }
  if (w > h) {
    return {maxDim, std::max(1, (int)((double)h / w * maxDim))};
  }
  return {std::max(1, (int)((double)w / h * maxDim)), maxDim};
}

// Bilinear downsample of an interleaved RGB buffer (srcW×srcH → dstW×dstH).
// Used when the required scale factor exceeds what a single JPEG DCT pass can provide.
static std::vector<uint8_t> bilinearResizeRgb(const std::vector<uint8_t>& src,
                                               int srcW, int srcH, int dstW, int dstH) {
  std::vector<uint8_t> dst(static_cast<size_t>(dstW * dstH) * 3);
  for (int dy = 0; dy < dstH; ++dy) {
    const float sy  = (dy + 0.5f) * srcH / dstH - 0.5f;
    const int   y0  = std::max(0, static_cast<int>(sy));
    const int   y1  = std::min(srcH - 1, y0 + 1);
    const float wy  = sy - static_cast<float>(y0);
    for (int dx = 0; dx < dstW; ++dx) {
      const float sx  = (dx + 0.5f) * srcW / dstW - 0.5f;
      const int   x0  = std::max(0, static_cast<int>(sx));
      const int   x1  = std::min(srcW - 1, x0 + 1);
      const float wx  = sx - static_cast<float>(x0);
      for (int c = 0; c < 3; ++c) {
        const float v00 = src[(y0 * srcW + x0) * 3 + c];
        const float v10 = src[(y0 * srcW + x1) * 3 + c];
        const float v01 = src[(y1 * srcW + x0) * 3 + c];
        const float v11 = src[(y1 * srcW + x1) * 3 + c];
        const float val = v00*(1-wx)*(1-wy) + v10*wx*(1-wy)
                        + v01*(1-wx)*wy     + v11*wx*wy;
        dst[(dy * dstW + dx) * 3 + c] =
            static_cast<uint8_t>(std::lround(std::clamp(val, 0.f, 255.f)));
      }
    }
  }
  return dst;
}

// Find the best JPEG DCT scaling factor: smallest output that is still >= [tw, th].
// Returns the decoded dimensions to use for tjDecompress2.
static std::pair<int,int> bestDctDecodeSize(int srcW, int srcH, int tw, int th) {
  int nf = 0;
  const tjscalingfactor* sf = tjGetScalingFactors(&nf);
  int bestW = srcW, bestH = srcH;
  for (int i = 0; i < nf; ++i) {
    const int sw = TJSCALED(srcW, sf[i]);
    const int sh = TJSCALED(srcH, sf[i]);
    if (sw >= tw && sh >= th && sw < bestW) {
      bestW = sw;
      bestH = sh;
    }
  }
  return {bestW, bestH};
}

static std::pair<int, int> readJpegDimensions(const std::vector<uint8_t>& jpeg) {
  tjhandle tj = tjInitDecompress();
  if (!tj) {
    return {0, 0};
  }
  int w = 0, h = 0, s = 0, cs = 0;
  tjDecompressHeader3(tj, jpeg.data(), (unsigned long)jpeg.size(), &w, &h, &s, &cs);
  tjDestroy(tj);
  return {w, h};
}

// ── ThumbnailCache ────────────────────────────────────────────────────────────

std::vector<uint8_t> ThumbnailCache::resizeJpeg(const std::vector<uint8_t>& src, int maxDim,
                                                float scale) {
  tjhandle tj = tjInitDecompress();
  if (!tj) {
    return src;
  }

  int w = 0, h = 0, subsamp = 0, colorspace = 0;
  if (tjDecompressHeader3(tj, src.data(), (unsigned long)src.size(), &w, &h, &subsamp,
                          &colorspace) < 0) {
    tjDestroy(tj);
    return src;
  }

  auto [tw, th] = scaleDimensions(w, h, maxDim);

  // Decode at the best available DCT scaling factor (≥ target size), then
  // software-resize if the DCT output is still larger than [tw, th].
  // This handles embedded JPEGs that are too large to decode directly to [tw, th]
  // in a single DCT pass (e.g. Canon CR2 6720×4480 embedded JPEG → 256px thumb).
  auto [decW, decH] = bestDctDecodeSize(w, h, tw, th);

  std::vector<uint8_t> rgb(static_cast<size_t>(decW * decH) * 3);
  if (tjDecompress2(tj, src.data(), (unsigned long)src.size(), rgb.data(), decW, 0, decH,
                    TJPF_RGB, TJFLAG_FASTDCT) < 0) {
    tjDestroy(tj);
    return src;
  }
  tjDestroy(tj);

  if (decW != tw || decH != th) {
    rgb = bilinearResizeRgb(rgb, decW, decH, tw, th);
  }

  if (scale < 0.999f) {
    for (auto& v : rgb) {
      v = static_cast<uint8_t>(std::lround(std::clamp(v * scale, 0.f, 255.f)));
    }
  }

  tjhandle tjc = tjInitCompress();
  if (!tjc) {
    return src;
  }

  unsigned char* outBuf = nullptr;
  unsigned long outSize = 0;
  if (tjCompress2(tjc, rgb.data(), tw, 0, th, TJPF_RGB, &outBuf, &outSize, TJSAMP_420, 85,
                  TJFLAG_FASTDCT) < 0) {
    tjDestroy(tjc);
    return src;
  }
  tjDestroy(tjc);

  std::vector<uint8_t> result(outBuf, outBuf + outSize);
  tjFree(outBuf);
  return result;
}

std::string ThumbnailCache::store(const std::string& hash, const std::vector<uint8_t>& jpegBytes,
                                  float scale) {
  if (hash.empty() || jpegBytes.empty()) {
    return "";
  }

  auto thumbData = resizeJpeg(jpegBytes, kMaxDim, scale);
  auto p = pathFor(hash);
  fs::create_directories(fs::path(p).parent_path());

  std::ofstream ofs(p, std::ios::binary);
  if (!ofs) {
    spdlog::warn("ThumbnailCache: cannot write {}", p);
    return "";
  }
  ofs.write(reinterpret_cast<const char*>(thumbData.data()),
            static_cast<std::streamsize>(thumbData.size()));
  return p;
}

bool ThumbnailCache::generate(int64_t photoId, const std::string& hash,
                              const std::vector<uint8_t>& thumbJpeg, PhotoRepository& repo,
                              float scale) {
  if (thumbJpeg.empty()) {
    return false;
  }

  auto existing = lookup(hash);
  if (!existing.empty()) {
    repo.updateThumb(photoId, existing, kMaxDim, kMaxDim, 0);
    return true;
  }

  auto path = store(hash, thumbJpeg, scale);
  if (path.empty()) {
    return false;
  }

  auto scaled = resizeJpeg(thumbJpeg, kMaxDim, scale);
  auto [w, h] = readJpegDimensions(scaled);
  if (w == 0) {
    w = kMaxDim;
  }
  if (h == 0) {
    h = kMaxDim;
  }

  repo.updateThumb(photoId, path, w, h,
                   (int64_t)std::filesystem::last_write_time(path).time_since_epoch().count());
  return true;
}

std::string ThumbnailCache::lookupMicro(const std::string& hash) const {
  const auto p = microPathFor(hash);
  return fs::exists(p) ? p : "";
}

std::string ThumbnailCache::storeMicro(const std::string& hash,
                                       const std::vector<uint8_t>& jpegBytes, float scale) {
  if (hash.empty() || jpegBytes.empty()) {
    return "";
  }

  const auto thumbData = resizeJpeg(jpegBytes, kMicroDim, scale);
  const auto p = microPathFor(hash);
  fs::create_directories(fs::path(p).parent_path());

  std::ofstream ofs(p, std::ios::binary);
  if (!ofs) {
    spdlog::warn("ThumbnailCache: cannot write micro {}", p);
    return "";
  }
  ofs.write(reinterpret_cast<const char*>(thumbData.data()),
            static_cast<std::streamsize>(thumbData.size()));
  return p;
}

bool ThumbnailCache::generateMicro(int64_t photoId, const std::string& hash,
                                   const std::vector<uint8_t>& thumbJpeg, PhotoRepository& repo,
                                   float scale) {
  if (thumbJpeg.empty()) {
    return false;
  }

  const auto existing = lookupMicro(hash);
  if (!existing.empty()) {
    repo.updateThumbMicro(photoId, existing);
    return true;
  }

  const auto path = storeMicro(hash, thumbJpeg, scale);
  if (path.empty()) {
    return false;
  }

  repo.updateThumbMicro(photoId, path);
  return true;
}

}  // namespace catalog
