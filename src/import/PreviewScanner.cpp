#include "PreviewScanner.h"
#include "FileScanner.h"
#include "HashDedup.h"
#include "RawDecoder.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace import_ns {

static std::optional<int64_t> dbDupCheck(catalog::Database& db, const std::string& hash) {
  std::lock_guard lk(db.mutex());
  return HashDedup::isDuplicate(db, hash);
}

PreviewScanner::PreviewScanner(catalog::Database& db) : db_(db) {}

PreviewScanner::~PreviewScanner() {
  cancel();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void PreviewScanner::start(const std::string& sourcePath,
                           ScanProgressCb progressCb,
                           ItemReadyCb itemCb) {
  if (running_) {
    return;
  }
  cancelled_ = false;
  running_   = true;
  thread_    = std::thread([this, sourcePath,
                            progressCb = std::move(progressCb),
                            itemCb     = std::move(itemCb)]() mutable {
    run(std::move(sourcePath), std::move(progressCb), std::move(itemCb));
  });
}

void PreviewScanner::cancel() {
  cancelled_ = true;
}

void PreviewScanner::run(std::string sourcePath, ScanProgressCb progressCb, ItemReadyCb itemCb) {
  spdlog::info("PreviewScanner: scanning {}", sourcePath);

  const auto files = FileScanner::scan(sourcePath);
  const int  total = static_cast<int>(files.size());
  spdlog::info("PreviewScanner: found {} files", total);

  int done = 0;
  for (const auto& sf : files) {
    if (cancelled_) {
      break;
    }
    if (progressCb) {
      progressCb(done, total);
    }
    ++done;

    try {
      const auto hash = HashDedup::fullHash(sf.path);
      if (dbDupCheck(db_, hash)) {
        spdlog::debug("PreviewScanner: duplicate skipped {}", sf.path);
        continue;
      }

      auto dec = RawDecoder::decode(sf.path);

      PreviewItem item;
      item.path        = sf.path;
      item.filename    = fs::path(sf.path).filename().string();
      item.captureTime = dec.exif.captureTime;
      item.thumbJpeg   = std::move(dec.thumbJpeg);

      if (itemCb) {
        itemCb(std::move(item));
      }
    } catch (const std::exception& ex) {
      spdlog::warn("PreviewScanner: error on {}: {}", sf.path, ex.what());
    }
  }

  if (progressCb) {
    progressCb(done, total);
  }
  running_ = false;
  spdlog::info("PreviewScanner: scan complete, processed {}/{}", done, total);
}

}  // namespace import_ns
