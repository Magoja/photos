#pragma once
#include "PhotoRepository.h"
#include <string>
#include <vector>
#include <cstdint>

namespace catalog {

// Manages on-disk JPEG thumbnail cache at:
//   ~/Library/Caches/PhotoLibrary/thumbs/{xx}/{hash}.jpg
// where {xx} = first 2 hex chars of hash
class ThumbnailCache {
 public:
  static constexpr int kMaxDim = 256;  // max thumbnail dimension

  explicit ThumbnailCache(const std::string& cacheRoot);

  // Generate and store a thumbnail from raw JPEG bytes.
  // Returns the path written, or "" on failure.
  std::string store(const std::string& hash, const std::vector<uint8_t>& jpegBytes);

  // Return cached path if it exists, "" otherwise.
  std::string lookup(const std::string& hash) const;

  // Generate thumbnail for a photo already in DB.
  // Reads thumb from decode result, stores it, updates DB.
  bool generate(int64_t photoId, const std::string& hash, const std::vector<uint8_t>& thumbJpeg,
                PhotoRepository& repo);

  // Resize JPEG to fit within maxDim×maxDim, preserving aspect ratio.
  static std::vector<uint8_t> resizeJpeg(const std::vector<uint8_t>& src, int maxDim);

 private:
  std::string root_;

  std::string pathFor(const std::string& hash) const;
};

}  // namespace catalog
