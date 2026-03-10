#pragma once
#include "ExportPreset.h"
#include "catalog/Database.h"
#include "catalog/PhotoRepository.h"
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <thread>

namespace export_ns {

using ProgressCb = std::function<void(int done, int total)>;
using DoneCb     = std::function<void(int exported, int errors)>;

class Exporter {
public:
    Exporter(catalog::PhotoRepository& repo, const ExportPreset& preset);
    ~Exporter();

    void setProgressCallback(ProgressCb cb) { progressCb_ = std::move(cb); }
    void setDoneCallback(DoneCb cb)         { doneCb_     = std::move(cb); }

    void start(const std::vector<int64_t>& photoIds);
    void cancel();
    bool isRunning() const { return running_; }

private:
    catalog::PhotoRepository& repo_;
    ExportPreset              preset_;
    ProgressCb         progressCb_;
    DoneCb             doneCb_;
    std::atomic<bool>  running_   {false};
    std::atomic<bool>  cancelled_ {false};
    std::thread        thread_;

    void run(std::vector<int64_t> ids);
    bool exportOne(const catalog::PhotoRecord& rec, const std::string& destDir);
};

} // namespace export_ns
