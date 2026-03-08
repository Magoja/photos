#pragma once
#include "catalog/Database.h"
#include "catalog/PhotoRepository.h"
#include "catalog/ThumbnailCache.h"
#include <string>
#include <atomic>
#include <functional>
#include <thread>
#include <memory>

namespace import_ns {

struct ImportStats {
    int total      = 0;
    int imported   = 0;
    int duplicates = 0;
    int errors     = 0;
};

struct ImportOptions {
    std::string sourcePath;
    std::string destPath;      // root of library storage
    std::string thumbCacheRoot;
    bool        copyFiles = true; // false = leave in place (linked import)
};

using ProgressCb = std::function<void(int done, int total, const std::string& current)>;
using DoneCb     = std::function<void(const ImportStats&)>;

class Importer {
public:
    Importer(catalog::Database& db, const ImportOptions& opts);
    ~Importer();

    void setProgressCallback(ProgressCb cb) { progressCb_ = std::move(cb); }
    void setDoneCallback(DoneCb cb)         { doneCb_     = std::move(cb); }

    // Start import in background thread; returns immediately
    void start();

    // Request cancel; import will stop after current file
    void cancel();

    bool isRunning() const { return running_; }
    ImportStats stats() const { return stats_; }

private:
    catalog::Database&   db_;
    ImportOptions        opts_;
    ProgressCb           progressCb_;
    DoneCb               doneCb_;
    std::atomic<bool>    running_   {false};
    std::atomic<bool>    cancelled_ {false};
    ImportStats          stats_;
    std::thread          thread_;

    void run();
    std::string destForDate(const std::string& isoDate) const;
};

} // namespace import_ns
