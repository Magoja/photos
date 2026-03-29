#pragma once
#include "catalog/Database.h"
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <cstdint>

namespace import_ns {

struct PreviewItem {
  std::string path;
  std::string filename;
  std::string captureTime;
  std::vector<uint8_t> thumbJpeg;
  int64_t previewId = 0;    // synthetic ID assigned by ImportDialog for texture cache
  void*   texture   = nullptr;  // MTLTexturePtr / ImTextureID, set on main thread after upload
};

using ScanProgressCb = std::function<void(int done, int total)>;
using ItemReadyCb    = std::function<void(PreviewItem)>;

class PreviewScanner {
 public:
  explicit PreviewScanner(catalog::Database& db);
  ~PreviewScanner();

  void start(const std::string& sourcePath, ScanProgressCb progressCb, ItemReadyCb itemCb);
  void cancel();
  bool isRunning() const { return running_; }

 private:
  void run(std::string sourcePath, ScanProgressCb progressCb, ItemReadyCb itemCb);

  catalog::Database& db_;
  std::thread        thread_;
  std::atomic<bool>  cancelled_{false};
  std::atomic<bool>  running_{false};
};

}  // namespace import_ns
