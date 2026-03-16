#pragma once
#include "PhotoRepository.h"
#include <string>
#include <vector>
#include <cstdint>

namespace catalog {

// Manages on-disk JPEG thumbnail cache at:
//   ~/Library/Caches/PhotoLibrary/thumbs/{xx}/{hash}.jpg       (standard, 256px)
//   ~/Library/Caches/PhotoLibrary/thumbs_micro/{xx}/{hash}.jpg (micro, 64px)
// where {xx} = first 2 hex chars of hash
class ThumbnailCache {
 public:
  static constexpr int kMaxDim   = 256;  // max standard thumbnail dimension
  static constexpr int kMicroDim = 64;   // max micro thumbnail dimension

  explicit ThumbnailCache(const std::string& cacheRoot);

  // Generate and store a standard thumbnail from raw JPEG bytes.
  // Returns the path written, or "" on failure.
  std::string store(const std::string& hash, const std::vector<uint8_t>& jpegBytes);

  // Return cached standard path if it exists, "" otherwise.
  std::string lookup(const std::string& hash) const;

  // Generate standard thumbnail for a photo already in DB.
  bool generate(int64_t photoId, const std::string& hash, const std::vector<uint8_t>& thumbJpeg,
                PhotoRepository& repo);

  // Generate and store a micro thumbnail from raw JPEG bytes.
  // Returns the path written, or "" on failure.
  std::string storeMicro(const std::string& hash, const std::vector<uint8_t>& jpegBytes);

  // Return cached micro path if it exists, "" otherwise.
  std::string lookupMicro(const std::string& hash) const;

  // Generate micro thumbnail for a photo already in DB.
  bool generateMicro(int64_t photoId, const std::string& hash,
                     const std::vector<uint8_t>& thumbJpeg, PhotoRepository& repo);

  // Resize JPEG to fit within maxDim×maxDim, preserving aspect ratio.
  static std::vector<uint8_t> resizeJpeg(const std::vector<uint8_t>& src, int maxDim);

 private:
  std::string root_;
  std::string microRoot_;

  std::string pathFor(const std::string& hash) const;
  std::string microPathFor(const std::string& hash) const;
};

}  // namespace catalog
