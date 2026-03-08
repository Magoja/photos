#pragma once
#include <string>
#include <functional>

namespace import_ns {

struct VolumeInfo {
    std::string uuid;
    std::string label;
    std::string mountPath;
    bool        isCamera = false; // has DCIM folder
};

using VolumeMountedCb   = std::function<void(const VolumeInfo&)>;
using VolumeUnmountedCb = std::function<void(const std::string& uuid)>;

class VolumeWatcher {
public:
    VolumeWatcher();
    ~VolumeWatcher();

    void setMountedCallback(VolumeMountedCb cb)     { mountedCb_   = std::move(cb); }
    void setUnmountedCallback(VolumeUnmountedCb cb) { unmountedCb_ = std::move(cb); }

    void start();
    void stop();

    // Called from DA callbacks — must be public
    void onMounted(const VolumeInfo&);
    void onUnmounted(const std::string& uuid);

private:
    struct Impl;
    Impl* impl_ = nullptr;

    VolumeMountedCb   mountedCb_;
    VolumeUnmountedCb unmountedCb_;
};

} // namespace import_ns
