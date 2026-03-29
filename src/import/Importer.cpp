#include "Importer.h"
#include "FileScanner.h"
#include "HashDedup.h"
#include "RawDecoder.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace catalog;

namespace import_ns {

Importer::Importer(Database& db, const ImportOptions& opts) : db_(db), opts_(opts) {}

Importer::~Importer() {
  cancel();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Importer::start() {
  if (running_) {
    return;
  }
  cancelled_ = false;
  running_ = true;
  thread_ = std::thread([this] { run(); });
}

void Importer::cancel() {
  cancelled_ = true;
}

std::string Importer::destForDate(const std::string& isoDate) const {
  std::string date = isoDate.size() >= 10 ? isoDate.substr(0, 10) : "unknown";
  return opts_.destPath + "/" + date;
}

// ── Per-file helpers ──────────────────────────────────────────────────────────

static std::optional<int64_t> dbDuplicateCheck(Database& db, const std::string& hash) {
  std::lock_guard lk(db.mutex());
  return HashDedup::isDuplicate(db, hash);
}

static std::optional<std::string> renameDestination(const std::string& srcPath,
                                                    const std::string& destDir) {
  const auto stem = fs::path(srcPath).stem().string();
  const auto ext  = fs::path(srcPath).extension().string();
  for (int i = 1; i < 1000; ++i) {
    const auto newName = stem + "-" + std::to_string(i) + ext;
    const auto newDest = destDir + "/" + newName;
    if (!fs::exists(newDest)) {
      std::error_code cp_ec;
      fs::copy_file(srcPath, newDest, cp_ec);
      if (cp_ec) {
        spdlog::warn("Import: rename-copy failed for {}: {}", srcPath, cp_ec.message());
        return std::nullopt;
      }
      return newDest;
    }
  }
  spdlog::warn("Import: no available rename slot for {}", fs::path(srcPath).filename().string());
  return std::nullopt;
}

static std::optional<std::string> copyToDestination(const std::string& srcPath,
                                                    const std::string& destDir,
                                                    const ConflictCb& conflictCb) {
  const auto filename = fs::path(srcPath).filename().string();
  const auto destFile = destDir + "/" + filename;

  std::error_code eq_ec;
  const bool alreadyThere = fs::equivalent(srcPath, destFile, eq_ec) && !eq_ec;
  if (alreadyThere) {
    return destFile;
  }

  fs::create_directories(destDir);

  if (fs::exists(destFile)) {
    const auto res = conflictCb ? conflictCb(filename, destDir)
                                : ConflictResolution::Skip;
    switch (res) {
      case ConflictResolution::Skip:
        return std::nullopt;
      case ConflictResolution::Overwrite: {
        std::error_code cp_ec;
        fs::copy_file(srcPath, destFile, fs::copy_options::overwrite_existing, cp_ec);
        if (cp_ec) {
          spdlog::warn("Import: overwrite failed for {}: {}", srcPath, cp_ec.message());
          return std::nullopt;
        }
        return destFile;
      }
      case ConflictResolution::Rename:
        return renameDestination(srcPath, destDir);
    }
  }

  std::error_code cp_ec;
  fs::copy_file(srcPath, destFile, fs::copy_options::skip_existing, cp_ec);
  if (cp_ec) {
    spdlog::warn("Import: copy failed for {}: {}", srcPath, cp_ec.message());
    return std::nullopt;
  }
  return destFile;
}

static PhotoRecord buildPhotoRecord(const DecodeResult& dec, int64_t folderId,
                                    const std::string& filename, const std::string& hash,
                                    int64_t fileSize) {
  PhotoRecord p;
  p.folderId = folderId;
  p.filename = filename;
  p.fileHash = hash;
  p.fileSize = fileSize;
  p.captureTime = dec.exif.captureTime;
  p.cameraMake = dec.exif.cameraMake;
  p.cameraModel = dec.exif.cameraModel;
  p.lensModel = dec.exif.lensModel;
  p.focalLengthMm = dec.exif.focalLengthMm;
  p.aperture = dec.exif.aperture;
  p.shutterSpeed = dec.exif.shutterSpeed;
  p.iso = dec.exif.iso;
  p.widthPx = dec.exif.widthPx;
  p.heightPx = dec.exif.heightPx;
  p.gpsLat = dec.exif.gpsLat;
  p.gpsLon = dec.exif.gpsLon;
  p.gpsAltM = dec.exif.gpsAltM;
  p.lumaScale = dec.lumaScale;
  return p;
}

// ── Import run loop ───────────────────────────────────────────────────────────

void Importer::run() {
  spdlog::info("Import started from: {}", opts_.sourcePath);

  std::vector<ScannedFile> files;
  if (opts_.selectedFiles.empty()) {
    files = FileScanner::scan(opts_.sourcePath);
  } else {
    files.reserve(opts_.selectedFiles.size());
    for (const auto& path : opts_.selectedFiles) {
      std::error_code ec;
      const auto sz = static_cast<int64_t>(fs::file_size(path, ec));
      files.push_back({.path = path, .size = ec ? 0 : sz});
    }
  }
  stats_.total = static_cast<int>(files.size());
  spdlog::info("Import: found {} files", stats_.total);

  PhotoRepository repo(db_);
  ThumbnailCache cache(opts_.thumbCacheRoot);
  std::unordered_set<uint64_t> sessionFp;

  int done = 0;
  for (auto& sf : files) {
    if (cancelled_) {
      spdlog::info("Import cancelled at file {}/{}", done, stats_.total);
      break;
    }
    if (progressCb_) {
      progressCb_(done, stats_.total, sf.path);
    }
    ++done;

    try {
      sessionFp.insert(HashDedup::fastFingerprint(sf.path));
      std::string hash = HashDedup::fullHash(sf.path);

      if (auto dup = dbDuplicateCheck(db_, hash)) {
        spdlog::debug("Import: duplicate skipped (photo_id={}) file={}", *dup,
                      fs::path(sf.path).filename().string());
        ++stats_.duplicates;
        continue;
      }

      auto dec = RawDecoder::decode(sf.path);

      std::string destFile;
      if (opts_.copyFiles) {
        auto result = copyToDestination(sf.path, destForDate(dec.exif.captureTime),
                                       opts_.conflictCb);
        if (!result) {
          ++stats_.errors;
          continue;
        }
        destFile = *result;
      } else {
        destFile = sf.path;
      }

      std::string folderRel = fs::path(destFile).parent_path().filename().string();
      int64_t fid;
      {
        std::lock_guard lk(db_.mutex());
        FolderRecord folder;
        folder.path = folderRel;
        folder.name = folderRel;
        fid = repo.upsertFolder(folder);
      }

      auto p = buildPhotoRecord(dec, fid, fs::path(destFile).filename().string(), hash, sf.size);
      int64_t pid;
      {
        std::lock_guard lk(db_.mutex());
        pid = repo.insertPhoto(p);
      }

      if (!dec.thumbJpeg.empty() && pid > 0) {
        cache.generate(pid, hash, dec.thumbJpeg, repo, dec.lumaScale);
        cache.generateMicro(pid, hash, dec.thumbJpeg, repo, dec.lumaScale);
      }

      ++stats_.imported;
      spdlog::debug("Import: imported {} ({})", p.filename, hash.substr(0, 8));

    } catch (const std::exception& ex) {
      spdlog::warn("Import error on {}: {}", sf.path, ex.what());
      ++stats_.errors;
    }
  }

  running_ = false;
  spdlog::info("Import done: imported={} duplicates={} errors={}", stats_.imported,
               stats_.duplicates, stats_.errors);
  if (doneCb_) {
    doneCb_(stats_);
  }
}

}  // namespace import_ns
