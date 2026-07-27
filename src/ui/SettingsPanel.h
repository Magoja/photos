#pragma once
#include "catalog/PhotoRepository.h"
#include <string>
#include <functional>

namespace ui {

class SettingsPanel {
 public:
  using ClearCacheCb = std::function<void()>;
  using ReimportMetadataCb = std::function<void()>;

  SettingsPanel(catalog::PhotoRepository& repo, const std::string& dbPath);

  void open();
  void render();

  bool isOpen() const { return open_; }

  void setClearCacheCallback(ClearCacheCb cb) { clearCacheCb_ = std::move(cb); }
  void setReimportMetadataCallback(ReimportMetadataCb cb) { reimportMetadataCb_ = std::move(cb); }

  // Report the outcome of a finished metadata reimport; opens a completion popup.
  void setReimportResult(int updated, int errors, int total);

 private:
  struct ReimportResult {
    int updated = 0;
    int errors = 0;
    int total = 0;
  };

  void renderReimportResultPopup();

  catalog::PhotoRepository& repo_;
  std::string dbPath_;
  bool open_ = false;
  ClearCacheCb clearCacheCb_;
  ReimportMetadataCb reimportMetadataCb_;
  ReimportResult reimportResult_;
  bool pendingOpenReimportPopup_ = false;
};

}  // namespace ui
