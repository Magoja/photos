// TextureManager.mm — LRU MTLTexture cache (non-ARC)
#include "TextureManager.h"
#include <turbojpeg.h>
#include <spdlog/spdlog.h>
#import <Metal/Metal.h>

namespace ui {

// In non-ARC .mm files, MTLTexturePtr = id<MTLTexture>

void TextureManager::init(MTLDevicePtr device) {
  device_ = device;
  placeholder_ = makePlaceholder();
}

TextureManager::~TextureManager() {
  std::lock_guard lk(mutex_);
  for (auto& [pid, pair] : lruMap_) {
    id<MTLTexture> tex = (id<MTLTexture>)pair.second.texture;
    if (tex)
      [tex release];
  }
  lruMap_.clear();
  lruList_.clear();
  if (placeholder_) {
    [((id<MTLTexture>)placeholder_) release];
    placeholder_ = nullptr;
  }
}

MTLTexturePtr TextureManager::makePlaceholder() {
  constexpr int W = 128, H = 128;
  id<MTLDevice> dev = (id<MTLDevice>)device_;
  MTLTextureDescriptor* desc =
    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                       width:W
                                                      height:H
                                                   mipmapped:NO];
  desc.storageMode = MTLStorageModeShared;
  desc.usage = MTLTextureUsageShaderRead;

  id<MTLTexture> tex = [dev newTextureWithDescriptor:desc];
  std::vector<uint8_t> pixels(W * H * 4, 128);
  for (int i = 3; i < W * H * 4; i += 4)
    pixels[i] = 255;
  [tex replaceRegion:MTLRegionMake2D(0, 0, W, H)
         mipmapLevel:0
           withBytes:pixels.data()
         bytesPerRow:W * 4];
  return (MTLTexturePtr)tex;  // retained by 'new'
}

MTLTexturePtr TextureManager::decodeAndCreate(const std::vector<uint8_t>& jpegBytes, int& outW,
                                              int& outH) {
  tjhandle tj = tjInitDecompress();
  if (!tj)
    return nullptr;

  int w = 0, h = 0, subsamp = 0, cs = 0;
  if (tjDecompressHeader3(tj, jpegBytes.data(), (unsigned long)jpegBytes.size(), &w, &h, &subsamp,
                          &cs) < 0) {
    tjDestroy(tj);
    return nullptr;
  }

  std::vector<uint8_t> rgba(w * h * 4);
  if (tjDecompress2(tj, jpegBytes.data(), (unsigned long)jpegBytes.size(), rgba.data(), w, 0, h,
                    TJPF_RGBA, TJFLAG_FASTDCT) < 0) {
    tjDestroy(tj);
    return nullptr;
  }
  tjDestroy(tj);

  id<MTLDevice> dev = (id<MTLDevice>)device_;
  MTLTextureDescriptor* desc =
    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                       width:w
                                                      height:h
                                                   mipmapped:NO];
  desc.storageMode = MTLStorageModeShared;
  desc.usage = MTLTextureUsageShaderRead;

  id<MTLTexture> tex = [dev newTextureWithDescriptor:desc];
  [tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
         mipmapLevel:0
           withBytes:rgba.data()
         bytesPerRow:w * 4];

  outW = w;
  outH = h;
  return (MTLTexturePtr)tex;  // retained by 'new'
}

MTLTexturePtr TextureManager::get(int64_t photoId) {
  std::lock_guard lk(mutex_);
  auto it = lruMap_.find(photoId);
  if (it == lruMap_.end())
    return placeholder_;

  lruList_.erase(it->second.first);
  lruList_.push_front(photoId);
  it->second.first = lruList_.begin();
  return it->second.second.texture;
}

bool TextureManager::upload(int64_t photoId, const std::vector<uint8_t>& jpegBytes) {
  int w = 0, h = 0;
  MTLTexturePtr tex = decodeAndCreate(jpegBytes, w, h);
  if (!tex)
    return false;

  std::lock_guard lk(mutex_);

  auto it = lruMap_.find(photoId);
  if (it != lruMap_.end()) {
    id<MTLTexture> old = (id<MTLTexture>)it->second.second.texture;
    if (old)
      [old release];
    lruList_.erase(it->second.first);
    lruMap_.erase(it);
  }

  while ((int)lruMap_.size() >= kLruMaxSlots)
    evictOldest();

  lruList_.push_front(photoId);
  TextureEntry entry{photoId, tex, w, h};
  lruMap_[photoId] = {lruList_.begin(), entry};
  return true;
}

void TextureManager::evict(int64_t photoId) {
  std::lock_guard lk(mutex_);
  auto it = lruMap_.find(photoId);
  if (it == lruMap_.end())
    return;
  id<MTLTexture> tex = (id<MTLTexture>)it->second.second.texture;
  if (tex)
    [tex release];
  lruList_.erase(it->second.first);
  lruMap_.erase(it);
}

std::pair<int, int> TextureManager::getSize(int64_t photoId) const {
  std::lock_guard lk(mutex_);
  auto it = lruMap_.find(photoId);
  if (it == lruMap_.end())
    return {0, 0};
  return {it->second.second.width, it->second.second.height};
}

void TextureManager::evictOldest() {
  if (lruList_.empty())
    return;
  int64_t oldest = lruList_.back();
  lruList_.pop_back();
  auto it = lruMap_.find(oldest);
  if (it != lruMap_.end()) {
    id<MTLTexture> tex = (id<MTLTexture>)it->second.second.texture;
    if (tex)
      [tex release];
    lruMap_.erase(it);
  }
}

}  // namespace ui
