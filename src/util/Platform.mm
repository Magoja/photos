// Platform.mm — macOS platform utilities
#include "Platform.h"
#include <spdlog/spdlog.h>

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace util {

static std::string nsStringToStd(NSString* s) {
  return s ? std::string([s UTF8String]) : "";
}

static std::string librarySubdir(NSSearchPathDirectory dir, const std::string& sub) {
  NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(dir, NSUserDomainMask, YES);
  NSString* base = paths.firstObject;
  if (!base) {
    return "";
  }
  NSString* full = [base
    stringByAppendingPathComponent:[NSString
                                     stringWithUTF8String:("com.jakeutil.photos/" + sub).c_str()]];
  std::string result = nsStringToStd(full);
  ensureDir(result);
  return result;
}

std::string cacheDir() {
  return librarySubdir(NSCachesDirectory, "");
}

std::string appSupportDir() {
  return librarySubdir(NSApplicationSupportDirectory, "");
}

std::string logsDir() {
  return librarySubdir(NSLibraryDirectory, "Logs/com.jakeutil.photos");
}

std::string desktopDir() {
  NSArray<NSString*>* paths =
    NSSearchPathForDirectoriesInDomains(NSDesktopDirectory, NSUserDomainMask, YES);
  return nsStringToStd(paths.firstObject);
}

std::optional<std::string> pickFolder() {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseDirectories = YES;
  panel.canChooseFiles = NO;
  panel.allowsMultipleSelection = NO;
  panel.title = @"Select Folder";

  if ([panel runModal] == NSModalResponseOK) {
    NSURL* url = panel.URLs.firstObject;
    if (url) {
      return nsStringToStd(url.path);
    }
  }
  return std::nullopt;
}

std::vector<std::string> pickFiles(const std::vector<std::string>& extensions) {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseDirectories = NO;
  panel.canChooseFiles = YES;
  panel.allowsMultipleSelection = YES;

  if (!extensions.empty()) {
    if (@available(macOS 12.0, *)) {
      NSMutableArray<UTType*>* types = [NSMutableArray array];
      for (auto& ext : extensions) {
        NSString* extStr = [NSString stringWithUTF8String:ext.c_str()];
        UTType* ut = [UTType typeWithFilenameExtension:extStr];
        if (ut) {
          [types addObject:ut];
        }
      }
      panel.allowedContentTypes = types;
    } else {
      NSMutableArray* types = [NSMutableArray array];
      for (auto& ext : extensions) {
        [types addObject:[NSString stringWithUTF8String:ext.c_str()]];
      }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      panel.allowedFileTypes = types;
#pragma clang diagnostic pop
    }
  }

  std::vector<std::string> result;
  if ([panel runModal] == NSModalResponseOK) {
    for (NSURL* url in panel.URLs) {
      result.push_back(nsStringToStd(url.path));
    }
  }
  return result;
}

bool ensureDir(const std::string& path) {
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec) {
    spdlog::warn("ensureDir failed for {}: {}", path, ec.message());
    return false;
  }
  return true;
}

}  // namespace util
