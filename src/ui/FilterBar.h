#pragma once

namespace ui {

enum class FilterMode { All, Picked };

class FilterBar {
public:
    FilterMode mode() const { return mode_; }
    bool       changed() const { return changed_; }

    // Call once per frame; returns true if filter changed
    bool render();

private:
    FilterMode mode_    = FilterMode::All;
    bool       changed_ = false;
};

} // namespace ui
