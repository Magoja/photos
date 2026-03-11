// VolumeWatcher.mm — DiskArbitration-based USB/volume detection
#include "VolumeWatcher.h"
#include <spdlog/spdlog.h>

#import <DiskArbitration/DiskArbitration.h>
#import <Foundation/Foundation.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace import_ns {

// ── Impl ──────────────────────────────────────────────────────────────────────
struct VolumeWatcher::Impl {
  DASessionRef session = nullptr;
  VolumeWatcher* owner = nullptr;
};

// ── DA callbacks (C linkage wrappers) ─────────────────────────────────────────
static void onDiskAppeared(DADiskRef disk, void* ctx) {
  auto* w = static_cast<VolumeWatcher*>(ctx);

  CFDictionaryRef desc = DADiskCopyDescription(disk);
  if (!desc)
    return;

  auto get = [&](CFStringRef key) -> std::string {
    CFTypeRef v = CFDictionaryGetValue(desc, key);
    if (!v)
      return {};
    if (CFGetTypeID(v) == CFStringGetTypeID()) {
      char buf[512] = {};
      CFStringGetCString((CFStringRef)v, buf, sizeof(buf), kCFStringEncodingUTF8);
      return buf;
    }
    return {};
  };

  // Only care about volumes with a mount path
  CFTypeRef urlRef = CFDictionaryGetValue(desc, kDADiskDescriptionVolumePathKey);
  if (!urlRef) {
    CFRelease(desc);
    return;
  }

  VolumeInfo info;

  // UUID
  CFTypeRef uuidRef = CFDictionaryGetValue(desc, kDADiskDescriptionVolumeUUIDKey);
  if (uuidRef && CFGetTypeID(uuidRef) == CFUUIDGetTypeID()) {
    CFStringRef uuidStr = CFUUIDCreateString(kCFAllocatorDefault, (CFUUIDRef)uuidRef);
    char buf[64] = {};
    CFStringGetCString(uuidStr, buf, sizeof(buf), kCFStringEncodingUTF8);
    info.uuid = buf;
    CFRelease(uuidStr);
  }
  if (info.uuid.empty()) {
    CFRelease(desc);
    return;
  }

  // Label
  info.label = get(kDADiskDescriptionVolumeNameKey);

  // Mount path
  char urlBuf[1024] = {};
  CFURLGetFileSystemRepresentation((CFURLRef)urlRef, true, (UInt8*)urlBuf, sizeof(urlBuf));
  info.mountPath = urlBuf;

  // Check for DCIM
  info.isCamera = fs::exists(info.mountPath + "/DCIM");

  CFRelease(desc);

  w->onMounted(info);
}

static void onDiskDisappeared(DADiskRef disk, void* ctx) {
  auto* w = static_cast<VolumeWatcher*>(ctx);

  CFDictionaryRef desc = DADiskCopyDescription(disk);
  if (!desc)
    return;

  std::string uuid;
  CFTypeRef uuidRef = CFDictionaryGetValue(desc, kDADiskDescriptionVolumeUUIDKey);
  if (uuidRef && CFGetTypeID(uuidRef) == CFUUIDGetTypeID()) {
    CFStringRef uuidStr = CFUUIDCreateString(kCFAllocatorDefault, (CFUUIDRef)uuidRef);
    char buf[64] = {};
    CFStringGetCString(uuidStr, buf, sizeof(buf), kCFStringEncodingUTF8);
    uuid = buf;
    CFRelease(uuidStr);
  }
  CFRelease(desc);

  if (!uuid.empty())
    w->onUnmounted(uuid);
}

// ── VolumeWatcher ─────────────────────────────────────────────────────────────
VolumeWatcher::VolumeWatcher() : impl_(new Impl) {
  impl_->owner = this;
}

VolumeWatcher::~VolumeWatcher() {
  stop();
  delete impl_;
}

void VolumeWatcher::start() {
  impl_->session = DASessionCreate(kCFAllocatorDefault);
  if (!impl_->session) {
    spdlog::error("VolumeWatcher: DASessionCreate failed");
    return;
  }

  DASessionScheduleWithRunLoop(impl_->session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

  // Match only writable, mountable volumes (not system)
  CFMutableDictionaryRef match = CFDictionaryCreateMutable(
    kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(match, kDADiskDescriptionVolumeMountableKey, kCFBooleanTrue);

  DARegisterDiskAppearedCallback(impl_->session, match, onDiskAppeared, this);
  DARegisterDiskDisappearedCallback(impl_->session, match, onDiskDisappeared, this);
  CFRelease(match);

  spdlog::info("VolumeWatcher started");
}

void VolumeWatcher::stop() {
  if (!impl_->session)
    return;
  DASessionUnscheduleFromRunLoop(impl_->session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  CFRelease(impl_->session);
  impl_->session = nullptr;
  spdlog::info("VolumeWatcher stopped");
}

void VolumeWatcher::onMounted(const VolumeInfo& info) {
  spdlog::info("Volume mounted uuid={} path={} label={}", info.uuid, info.mountPath, info.label);
  if (mountedCb_)
    mountedCb_(info);
}

void VolumeWatcher::onUnmounted(const std::string& uuid) {
  spdlog::info("Volume unmounted uuid={}", uuid);
  if (unmountedCb_)
    unmountedCb_(uuid);
}

}  // namespace import_ns
