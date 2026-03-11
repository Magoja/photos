#pragma once
#include "catalog/PhotoRepository.h"
#include <string>
#include <optional>
#include <cstdint>

namespace import_ns {

struct FileHash {
  uint64_t fast;     // XXH3-64 fast fingerprint (first+last 64 KB + size)
  std::string full;  // XXH3-128 hex, computed only when needed
};

class HashDedup {
 public:
  // Compute fast fingerprint (XH3-64 of first+last 64KB + file size)
  static uint64_t fastFingerprint(const std::string& path);

  // Compute full XXH3-128 hash of entire file; result as hex string
  static std::string fullHash(const std::string& path);

  // Check if the hash already exists in the DB
  // Returns photo_id if duplicate, nullopt otherwise
  static std::optional<int64_t> isDuplicate(catalog::Database& db, const std::string& hash);
};

}  // namespace import_ns
