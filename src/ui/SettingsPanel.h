#pragma once
#include "catalog/PhotoRepository.h"
#include <string>
#include <functional>

namespace ui {

class SettingsPanel {
 public:
  using ClearCacheCb = std::function<void()>;

  SettingsPanel(catalog::PhotoRepository& repo, const std::string& dbPath);

  void open();
  void render();

  bool isOpen() const { return open_; }

  void setClearCacheCallback(ClearCacheCb cb) { clearCacheCb_ = std::move(cb); }

 private:
  catalog::PhotoRepository& repo_;
  std::string dbPath_;
  bool open_ = false;
  ClearCacheCb clearCacheCb_;
};

}  // namespace ui
