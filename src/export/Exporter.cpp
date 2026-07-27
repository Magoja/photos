#include "Exporter.h"
#include "ExifWriter.h"
#include "util/PixelPipeline.h"
#include "catalog/EditSettings.h"
#include "import/ImageDecoder.h"
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fs = std::filesystem;

using namespace catalog;

namespace export_ns {

Exporter::Exporter(PhotoRepository& repo, const ExportPreset& preset)
  : repo_(repo), preset_(preset) {}

Exporter::~Exporter() {
  cancel();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Exporter::start(const std::vector<int64_t>& photoIds) {
  if (running_) {
    return;
  }
  cancelled_ = false;
  running_ = true;
  thread_ = std::thread([this, ids = photoIds] { run(ids); });
}

void Exporter::cancel() {
  cancelled_ = true;
}

// ── Pixel-level edit helpers ──────────────────────────────────────────────────

// Delegated to util::applyAdjustments (shared with EditView preview pipeline).
static std::vector<uint8_t> applyAdjustments(const std::vector<uint8_t>& src, int w, int h,
                                             const EditSettings& s) {
  return util::applyAdjustments(src, w, h, s);
}

static std::vector<uint8_t> compressToJpeg(const std::vector<uint8_t>& rgb, int w, int h,
                                           int quality) {
  tjhandle tjc = tjInitCompress();
  if (!tjc) {
    return {};
  }
  unsigned char* out = nullptr;
  unsigned long outSz = 0;
  tjCompress2(tjc, rgb.data(), w, 0, h, TJPF_RGB, &out, &outSz, TJSAMP_420, quality,
              TJFLAG_FASTDCT);
  tjDestroy(tjc);
  if (!out) {
    return {};
  }
  std::vector<uint8_t> result(out, out + outSz);
  tjFree(out);
  return result;
}

// ── Exporter ──────────────────────────────────────────────────────────────────

bool Exporter::exportOne(const PhotoRecord& rec, const fs::path& destPath) {
  const std::string srcPath = repo_.fullPathFor(rec.folderId, rec.filename);

  // 1. Decode source pixels (JPEG via TurboJPEG or RAW via LibRaw)
  const auto decoded = import_ns::decodeSource(srcPath);
  if (!decoded) {
    return false;
  }

  // 2. Apply EditSettings
  const EditSettings settings = EditSettings::fromJson(rec.editSettings);
  const auto adjusted = applyAdjustments(decoded->rgb, decoded->w, decoded->h, settings);

  // 3. Apply crop + straighten
  int outW = decoded->w, outH = decoded->h;
  const auto cropped =
    util::cropAndRotatePixels(adjusted, decoded->w, decoded->h, settings.crop, outW, outH);

  // 4. Compress to JPEG
  auto jpeg = compressToJpeg(cropped, outW, outH, preset_.quality);
  if (jpeg.empty()) {
    spdlog::warn("Export: JPEG compress failed for {}", srcPath);
    return false;
  }

  // 5. Inject EXIF
  const auto exifPayload = buildExifPayload(rec);
  const auto output = injectExifApp1(jpeg, exifPayload);

  // 6. Write output file
  std::ofstream ofs(destPath, std::ios::binary);
  if (!ofs) {
    spdlog::warn("Export: cannot write {}", destPath.string());
    return false;
  }
  ofs.write(reinterpret_cast<const char*>(output.data()),
            static_cast<std::streamsize>(output.size()));
  spdlog::info("Export: wrote {}", destPath.string());
  return true;
}

void Exporter::run(std::vector<int64_t> ids) {
  std::error_code ec;
  fs::create_directories(preset_.targetPath, ec);
  if (!fs::is_directory(preset_.targetPath)) {
    spdlog::error("Export: output dir unavailable '{}': {}", preset_.targetPath,
                  ec ? ec.message() : "not a directory");
    running_ = false;
    if (doneCb_) {
      doneCb_(0, static_cast<int>(ids.size()));
    }
    return;
  }

  int exported = 0, errors = 0;
  bool overwriteAll = false;
  bool skipAll = false;

  for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
    if (cancelled_) {
      break;
    }
    if (progressCb_) {
      progressCb_(i, static_cast<int>(ids.size()));
    }

    const auto rec = repo_.findById(ids[i]);
    if (!rec) {
      ++errors;
      continue;
    }

    const std::string stem = fs::path(rec->filename).stem().string();
    const fs::path destPath = fs::path(preset_.targetPath) / (stem + ".jpg");

    if (fs::exists(destPath)) {
      if (skipAll) {
        continue;
      }
      if (!overwriteAll) {
        const util::OverwriteChoice choice =
          conflictCb_ ? conflictCb_(destPath.filename().string()) : util::OverwriteChoice::Skip;
        if (choice == util::OverwriteChoice::SkipAll) {
          skipAll = true;
          continue;
        }
        if (choice == util::OverwriteChoice::Skip) {
          continue;
        }
        if (choice == util::OverwriteChoice::OverwriteAll) {
          overwriteAll = true;
        }
      }
    }

    if (exportOne(*rec, destPath)) {
      ++exported;
    } else {
      ++errors;
    }
  }

  running_ = false;
  spdlog::info("Export done: {} exported, {} errors", exported, errors);
  if (doneCb_) {
    doneCb_(exported, errors);
  }
}

}  // namespace export_ns
