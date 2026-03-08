#include "HashDedup.h"
#include "catalog/Database.h"
#include <xxhash.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>

namespace import_ns {

static constexpr size_t kChunkSize = 64 * 1024; // 64 KB

uint64_t HashDedup::fastFingerprint(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;

    // Get file size
    std::fseek(f, 0, SEEK_END);
    int64_t size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    XXH3_state_t* state = XXH3_createState();
    XXH3_64bits_reset(state);

    // Hash file size
    XXH3_64bits_update(state, &size, sizeof(size));

    // Hash first chunk
    std::vector<uint8_t> buf(kChunkSize);
    size_t n = std::fread(buf.data(), 1, kChunkSize, f);
    XXH3_64bits_update(state, buf.data(), n);

    // Hash last chunk (if file is bigger than 2 chunks)
    if (size > static_cast<int64_t>(kChunkSize * 2)) {
        std::fseek(f, -static_cast<long>(kChunkSize), SEEK_END);
        n = std::fread(buf.data(), 1, kChunkSize, f);
        XXH3_64bits_update(state, buf.data(), n);
    }

    std::fclose(f);
    uint64_t result = XXH3_64bits_digest(state);
    XXH3_freeState(state);
    return result;
}

std::string HashDedup::fullHash(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";

    XXH3_state_t* state = XXH3_createState();
    XXH3_128bits_reset(state);

    std::vector<uint8_t> buf(256 * 1024);
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0)
        XXH3_128bits_update(state, buf.data(), n);

    std::fclose(f);
    XXH128_hash_t h = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    char hex[33];
    std::snprintf(hex, sizeof(hex),
                  "%016llx%016llx",
                  (unsigned long long)h.high64,
                  (unsigned long long)h.low64);
    return hex;
}

std::optional<int64_t> HashDedup::isDuplicate(catalog::Database& db,
                                               const std::string& hash)
{
    auto s = db.prepare("SELECT id FROM photos WHERE file_hash=? LIMIT 1");
    s.bind(1, hash);
    if (s.step()) return s.getInt64(0);
    return std::nullopt;
}

} // namespace import_ns
