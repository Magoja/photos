#include "Importer.h"
#include "FileScanner.h"
#include "HashDedup.h"
#include "RawDecoder.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <unordered_set>
#include <cstdio>

namespace fs = std::filesystem;
using namespace catalog;

namespace import_ns {

Importer::Importer(Database& db, const ImportOptions& opts)
    : db_(db), opts_(opts)
{}

Importer::~Importer() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void Importer::start() {
    if (running_) return;
    cancelled_ = false;
    running_   = true;
    thread_ = std::thread([this]{ run(); });
}

void Importer::cancel() {
    cancelled_ = true;
}

std::string Importer::destForDate(const std::string& isoDate) const {
    // isoDate: "YYYY-MM-DDTHH:MM:SS" or "YYYY-MM-DD..."
    std::string date = isoDate.size() >= 10
        ? isoDate.substr(0, 10) : "unknown";
    return opts_.destPath + "/" + date;
}

void Importer::run() {
    spdlog::info("Import started from: {}", opts_.sourcePath);

    // 1. Scan source
    auto files = FileScanner::scan(opts_.sourcePath);
    stats_.total = static_cast<int>(files.size());
    spdlog::info("Import: found {} files", stats_.total);

    PhotoRepository repo(db_);
    ThumbnailCache  cache(opts_.thumbCacheRoot);

    // In-session fast-fingerprint set to avoid recomputing full hash
    std::unordered_set<uint64_t> sessionFp;

    int done = 0;
    for (auto& sf : files) {
        if (cancelled_) {
            spdlog::info("Import cancelled at file {}/{}", done, stats_.total);
            break;
        }

        if (progressCb_) progressCb_(done, stats_.total, sf.path);
        ++done;

        try {
            // Fast fingerprint check
            uint64_t fp = HashDedup::fastFingerprint(sf.path);
            bool fpSeen = !sessionFp.insert(fp).second;

            // Full hash
            std::string hash = HashDedup::fullHash(sf.path);

            // DB duplicate check
            std::optional<int64_t> dup;
            {
                std::lock_guard lk(db_.mutex());
                dup = HashDedup::isDuplicate(db_, hash);
            }
            if (dup.has_value()) {
                spdlog::debug("Import: duplicate skipped (photo_id={}) file={}",
                              *dup, fs::path(sf.path).filename().string());
                ++stats_.duplicates;
                continue;
            }
            (void)fpSeen; // fast fingerprint only for in-session dedup hint

            // Decode RAW/JPEG
            auto dec = RawDecoder::decode(sf.path);

            // Determine destination file path
            std::string destFile;
            if (opts_.copyFiles) {
                std::string destDir = destForDate(dec.exif.captureTime);
                destFile = destDir + "/" + fs::path(sf.path).filename().string();

                std::error_code eq_ec;
                bool alreadyThere = fs::equivalent(sf.path, destFile, eq_ec) && !eq_ec;
                if (!alreadyThere) {
                    fs::create_directories(destDir);
                    std::error_code cp_ec;
                    fs::copy_file(sf.path, destFile,
                                  fs::copy_options::skip_existing, cp_ec);
                    if (cp_ec) {
                        spdlog::warn("Import: copy failed for {}: {}", sf.path, cp_ec.message());
                        ++stats_.errors;
                        continue;
                    }
                }
            } else {
                destFile = sf.path;
            }

            // Upsert folder — store relative path (just the date subfolder name)
            std::string folderRel  = fs::path(destFile).parent_path().filename().string();

            int64_t fid;
            {
                std::lock_guard lk(db_.mutex());
                FolderRecord folder;
                folder.path = folderRel;
                folder.name = folderRel;
                fid = repo.upsertFolder(folder);
            }

            // Insert photo record
            PhotoRecord p;
            p.folderId      = fid;
            p.filename      = fs::path(destFile).filename().string();
            p.fileHash      = hash;
            p.fileSize      = sf.size;
            p.captureTime   = dec.exif.captureTime;
            p.cameraMake    = dec.exif.cameraMake;
            p.cameraModel   = dec.exif.cameraModel;
            p.lensModel     = dec.exif.lensModel;
            p.focalLengthMm = dec.exif.focalLengthMm;
            p.aperture      = dec.exif.aperture;
            p.shutterSpeed  = dec.exif.shutterSpeed;
            p.iso           = dec.exif.iso;
            p.widthPx       = dec.exif.widthPx;
            p.heightPx      = dec.exif.heightPx;
            p.gpsLat        = dec.exif.gpsLat;
            p.gpsLon        = dec.exif.gpsLon;
            p.gpsAltM       = dec.exif.gpsAltM;

            int64_t pid;
            {
                std::lock_guard lk(db_.mutex());
                pid = repo.insertPhoto(p);
            }

            // Generate thumbnail
            if (!dec.thumbJpeg.empty() && pid > 0) {
                cache.generate(pid, hash, dec.thumbJpeg, repo);
            }

            ++stats_.imported;
            spdlog::debug("Import: imported {} ({})", p.filename, hash.substr(0, 8));

        } catch (const std::exception& ex) {
            spdlog::warn("Import error on {}: {}", sf.path, ex.what());
            ++stats_.errors;
        }
    }

    running_ = false;
    spdlog::info("Import done: imported={} duplicates={} errors={}",
                 stats_.imported, stats_.duplicates, stats_.errors);
    if (doneCb_) doneCb_(stats_);
}

} // namespace import_ns
