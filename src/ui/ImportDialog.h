#pragma once
#include "import/VolumeWatcher.h"
#include "import/Importer.h"
#include "catalog/Database.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

namespace ui {

class ImportDialog {
public:
    using DoneCb = std::function<void()>;

    ImportDialog(catalog::Database& db);

    void setDoneCallback(DoneCb cb) { doneCb_ = std::move(cb); }

    // Set source and destination paths to begin preview
    void open(const std::string& sourcePath,
              const std::string& destPath,
              const std::string& thumbCacheRoot);
    void close();
    bool isOpen() const { return open_; }

    void render();

private:
    void startImport();

    catalog::Database&            db_;
    DoneCb                        doneCb_;
    bool                          open_  = false;

    std::string                   sourcePath_;
    std::string                   destPath_;
    std::string                   thumbRoot_;
    bool                          copyFiles_ = true;

    std::unique_ptr<import_ns::Importer> importer_;
    import_ns::ImportStats                stats_;
    std::atomic<int>                      totalFiles_{0};
    std::atomic<int>                      doneFiles_{0};
    std::mutex                            progressMtx_;
    std::string                           currentFile_;
    std::atomic<bool>                     importing_{false};
    std::atomic<bool>                     finished_{false};
};

} // namespace ui
