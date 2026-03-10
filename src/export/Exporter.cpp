#include "Exporter.h"
#include "import/RawDecoder.h"
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;
using namespace catalog;

namespace export_ns {

Exporter::Exporter(PhotoRepository& repo, const ExportPreset& preset)
    : repo_(repo), preset_(preset) {}

Exporter::~Exporter() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void Exporter::start(const std::vector<int64_t>& photoIds) {
    if (running_) return;
    cancelled_ = false;
    running_   = true;
    thread_ = std::thread([this, ids = photoIds]{ run(ids); });
}

void Exporter::cancel() { cancelled_ = true; }

// Resize & recompress a JPEG buffer
static std::vector<uint8_t> resizeJpeg(const std::vector<uint8_t>& src,
                                        int maxW, int maxH, int quality)
{
    tjhandle tj = tjInitDecompress();
    if (!tj) return {};

    int w = 0, h = 0, subsamp = 0, cs = 0;
    if (tjDecompressHeader3(tj, src.data(), (unsigned long)src.size(),
                             &w, &h, &subsamp, &cs) < 0) {
        tjDestroy(tj); return {};
    }

    int tw = w, th = h;
    if (maxW > 0 || maxH > 0) {
        int limit = std::max(maxW > 0 ? maxW : INT_MAX,
                             maxH > 0 ? maxH : INT_MAX);
        // Use min(maxW,maxH) as longest-edge limit
        limit = maxW > 0 && maxH > 0 ? std::max(maxW, maxH) : (maxW > 0 ? maxW : maxH);
        if (w > limit || h > limit) {
            if (w > h) { tw = limit; th = (int)((double)h / w * limit); }
            else       { th = limit; tw = (int)((double)w / h * limit); }
        }
    }
    if (tw < 1) tw = 1; if (th < 1) th = 1;

    std::vector<uint8_t> rgb(tw * th * 3);
    if (tjDecompress2(tj, src.data(), (unsigned long)src.size(),
                      rgb.data(), tw, 0, th, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
        tjDestroy(tj); return {};
    }
    tjDestroy(tj);

    tjhandle tjc = tjInitCompress();
    if (!tjc) return {};
    unsigned char* out = nullptr;
    unsigned long  outSz = 0;
    tjCompress2(tjc, rgb.data(), tw, 0, th, TJPF_RGB,
                &out, &outSz, TJSAMP_420, quality, TJFLAG_FASTDCT);
    tjDestroy(tjc);
    std::vector<uint8_t> result(out, out + outSz);
    tjFree(out);
    return result;
}

bool Exporter::exportOne(const PhotoRecord& rec, const std::string& destDir)
{
    // Build source path using repository's library root + relative folder
    std::string srcPath = repo_.fullPathFor(rec.folderId, rec.filename);

    // Decode
    auto dec = import_ns::RawDecoder::decode(srcPath);
    if (!dec.ok || dec.thumbJpeg.empty()) {
        spdlog::warn("Export: decode failed for {}", srcPath);
        return false;
    }

    // Resize
    auto outJpeg = resizeJpeg(dec.thumbJpeg,
        preset_.maxWidth, preset_.maxHeight, preset_.quality);
    if (outJpeg.empty()) {
        spdlog::warn("Export: resize failed for {}", srcPath);
        return false;
    }

    // Destination
    fs::path destPath = fs::path(destDir) /
        (fs::path(rec.filename).stem().string() + ".jpg");
    std::ofstream ofs(destPath, std::ios::binary);
    if (!ofs) { spdlog::warn("Export: cannot write {}", destPath.string()); return false; }
    ofs.write(reinterpret_cast<const char*>(outJpeg.data()),
              static_cast<std::streamsize>(outJpeg.size()));
    return true;
}

void Exporter::run(std::vector<int64_t> ids) {
    fs::create_directories(preset_.targetPath);

    int exported = 0, errors = 0;
    for (int i = 0; i < (int)ids.size(); ++i) {
        if (cancelled_) break;
        if (progressCb_) progressCb_(i, (int)ids.size());

        auto rec = repo_.findById(ids[i]);
        if (!rec) { ++errors; continue; }

        if (exportOne(*rec, preset_.targetPath)) ++exported;
        else ++errors;
    }

    running_ = false;
    spdlog::info("Export done: {} exported, {} errors", exported, errors);
    if (doneCb_) doneCb_(exported, errors);
}

} // namespace export_ns
