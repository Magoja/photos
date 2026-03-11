#include "HashDedup.h"
#include "catalog/Database.h"
#include <xxhash.h>
#include <spdlog/spdlog.h>
#include <format>
#include <vector>
#include <cstdio>

namespace import_ns {

static constexpr size_t kChunkSize = 64 * 1024;  // 64 KB

static int64_t fileSize(FILE* f) {
  std::fseek(f, 0, SEEK_END);
  int64_t sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  return sz;
}

static size_t readChunk(FILE* f, std::vector<uint8_t>& buf, long offset = 0) {
  if (offset) {
    std::fseek(f, offset, SEEK_END);
  }
  return std::fread(buf.data(), 1, kChunkSize, f);
}

uint64_t HashDedup::fastFingerprint(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return 0;
  }

  XXH3_state_t* state = XXH3_createState();
  XXH3_64bits_reset(state);

  int64_t sz = fileSize(f);
  XXH3_64bits_update(state, &sz, sizeof(sz));

  std::vector<uint8_t> buf(kChunkSize);
  size_t n = readChunk(f, buf);
  XXH3_64bits_update(state, buf.data(), n);

  if (sz > static_cast<int64_t>(kChunkSize * 2)) {
    n = readChunk(f, buf, -static_cast<long>(kChunkSize));
    XXH3_64bits_update(state, buf.data(), n);
  }

  std::fclose(f);
  uint64_t result = XXH3_64bits_digest(state);
  XXH3_freeState(state);
  return result;
}

std::string HashDedup::fullHash(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return "";
  }

  XXH3_state_t* state = XXH3_createState();
  XXH3_128bits_reset(state);

  std::vector<uint8_t> buf(256 * 1024);
  size_t n;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
    XXH3_128bits_update(state, buf.data(), n);
  }

  std::fclose(f);
  XXH128_hash_t h = XXH3_128bits_digest(state);
  XXH3_freeState(state);
  return std::format("{:016x}{:016x}", h.high64, h.low64);
}

std::optional<int64_t> HashDedup::isDuplicate(catalog::Database& db, const std::string& hash) {
  auto s = db.prepare("SELECT id FROM photos WHERE file_hash=? LIMIT 1");
  s.bind(1, hash);
  if (s.step()) {
    return s.getInt64(0);
  }
  return std::nullopt;
}

}  // namespace import_ns
