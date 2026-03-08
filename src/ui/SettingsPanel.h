#pragma once
#include "catalog/PhotoRepository.h"
#include <string>

namespace ui {

class SettingsPanel {
public:
    SettingsPanel(catalog::PhotoRepository& repo, const std::string& dbPath);

    void open();
    void render();

    bool isOpen() const { return open_; }

private:
    catalog::PhotoRepository& repo_;
    std::string               dbPath_;
    bool                      open_ = false;
    char                      libraryRoot_[1024]   = {};
    char                      thumbCacheDir_[1024] = {};
};

} // namespace ui
