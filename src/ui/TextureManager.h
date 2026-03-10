#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>

// Forward declare Metal types without Obj-C header
#ifdef __OBJC__
#import <Metal/Metal.h>
using MTLTexturePtr = id<MTLTexture>;
using MTLDevicePtr  = id<MTLDevice>;
#else
using MTLTexturePtr = void*;
using MTLDevicePtr  = void*;
#endif

namespace ui {

static constexpr int kLruMaxSlots = 2000;

struct TextureEntry {
    int64_t       photoId   = 0;
    MTLTexturePtr texture   = nullptr;
    int           width     = 0;
    int           height    = 0;
};

class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager();

    void init(MTLDevicePtr device);

    // Returns texture for photoId, or placeholder if not cached.
    // Async decode should be triggered separately.
    MTLTexturePtr get(int64_t photoId);

    // Upload decoded JPEG bytes and store under photoId.
    // Must be called from the main (Metal) thread.
    bool upload(int64_t photoId, const std::vector<uint8_t>& jpegBytes);

    // Explicitly evict a photo's texture
    void evict(int64_t photoId);

    // Returns stored pixel dimensions; {0,0} if not in cache
    std::pair<int,int> getSize(int64_t photoId) const;

    MTLTexturePtr placeholder() const { return placeholder_; }

    size_t cacheSize() const { return lruMap_.size(); }

private:
    MTLDevicePtr  device_      = nullptr;
    MTLTexturePtr placeholder_ = nullptr;

    // LRU: list front = most recent, back = oldest
    std::list<int64_t>                          lruList_;
    std::unordered_map<int64_t, std::pair<
        std::list<int64_t>::iterator,
        TextureEntry>>                          lruMap_;
    mutable std::mutex                          mutex_;

    void evictOldest();
    MTLTexturePtr makePlaceholder();
    MTLTexturePtr decodeAndCreate(const std::vector<uint8_t>& jpegBytes,
                                   int& outW, int& outH);
};

} // namespace ui
