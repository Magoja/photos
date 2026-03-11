#include "ThumbnailCache.h"
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace catalog {

ThumbnailCache::ThumbnailCache(const std::string& cacheRoot)
    : root_(cacheRoot)
{
    fs::create_directories(root_);
}

std::string ThumbnailCache::pathFor(const std::string& hash) const {
    if (hash.size() < 2) return root_ + "/00/" + hash + ".jpg";
    return root_ + "/" + hash.substr(0, 2) + "/" + hash + ".jpg";
}

std::string ThumbnailCache::lookup(const std::string& hash) const {
    auto p = pathFor(hash);
    return fs::exists(p) ? p : "";
}

// ── libjpeg-turbo helpers ─────────────────────────────────────────────────────

static std::pair<int,int> scaleDimensions(int w, int h, int maxDim) {
    if (w <= maxDim && h <= maxDim) return {w, h};
    if (w > h) return { maxDim, std::max(1, (int)((double)h / w * maxDim)) };
    return { std::max(1, (int)((double)w / h * maxDim)), maxDim };
}

static std::pair<int,int> readJpegDimensions(const std::vector<uint8_t>& jpeg) {
    tjhandle tj = tjInitDecompress();
    if (!tj) return {0, 0};
    int w = 0, h = 0, s = 0, cs = 0;
    tjDecompressHeader3(tj, jpeg.data(), (unsigned long)jpeg.size(), &w, &h, &s, &cs);
    tjDestroy(tj);
    return {w, h};
}

// ── ThumbnailCache ────────────────────────────────────────────────────────────

std::vector<uint8_t> ThumbnailCache::resizeJpeg(const std::vector<uint8_t>& src,
                                                  int maxDim)
{
    tjhandle tj = tjInitDecompress();
    if (!tj) return src;

    int w = 0, h = 0, subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(tj, src.data(), (unsigned long)src.size(),
                             &w, &h, &subsamp, &colorspace) < 0) {
        tjDestroy(tj);
        return src;
    }

    auto [tw, th] = scaleDimensions(w, h, maxDim);

    std::vector<uint8_t> rgb(tw * th * 3);
    if (tjDecompress2(tj, src.data(), (unsigned long)src.size(),
                      rgb.data(), tw, 0, th, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
        tjDestroy(tj);
        return src;
    }
    tjDestroy(tj);

    tjhandle tjc = tjInitCompress();
    if (!tjc) return src;

    unsigned char* outBuf = nullptr;
    unsigned long  outSize = 0;
    if (tjCompress2(tjc, rgb.data(), tw, 0, th, TJPF_RGB,
                    &outBuf, &outSize, TJSAMP_420, 85, TJFLAG_FASTDCT) < 0) {
        tjDestroy(tjc);
        return src;
    }
    tjDestroy(tjc);

    std::vector<uint8_t> result(outBuf, outBuf + outSize);
    tjFree(outBuf);
    return result;
}

std::string ThumbnailCache::store(const std::string& hash,
                                   const std::vector<uint8_t>& jpegBytes)
{
    if (hash.empty() || jpegBytes.empty()) return "";

    auto thumbData = resizeJpeg(jpegBytes, kMaxDim);
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

bool ThumbnailCache::generate(int64_t photoId,
                               const std::string& hash,
                               const std::vector<uint8_t>& thumbJpeg,
                               PhotoRepository& repo)
{
    if (thumbJpeg.empty()) return false;

    auto existing = lookup(hash);
    if (!existing.empty()) {
        repo.updateThumb(photoId, existing, kMaxDim, kMaxDim, 0);
        return true;
    }

    auto path = store(hash, thumbJpeg);
    if (path.empty()) return false;

    auto scaled = resizeJpeg(thumbJpeg, kMaxDim);
    auto [w, h] = readJpegDimensions(scaled);
    if (w == 0) w = kMaxDim;
    if (h == 0) h = kMaxDim;

    repo.updateThumb(photoId, path, w, h,
                     (int64_t)std::filesystem::last_write_time(path)
                         .time_since_epoch().count());
    return true;
}

} // namespace catalog
