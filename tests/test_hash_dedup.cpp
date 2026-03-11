#include <catch2/catch_test_macros.hpp>
#include "import/HashDedup.h"
#include "catalog/Database.h"
#include "catalog/Schema.h"
#include "catalog/PhotoRepository.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace fs = std::filesystem;
using namespace import_ns;
using namespace catalog;

// ── helpers ───────────────────────────────────────────────────────────────────
static fs::path writeTempFile(const std::string& name, const std::string& content) {
  auto p = fs::temp_directory_path() / name;
  std::ofstream f(p, std::ios::binary);
  f.write(content.data(), content.size());
  return p;
}

struct TempDb2 {
  fs::path path;
  std::unique_ptr<Database> db;
  TempDb2() {
    path = fs::temp_directory_path() / ("hd_" + std::to_string(std::rand()) + ".db");
    db = std::make_unique<Database>(path.string());
    Schema::apply(*db);
  }
  ~TempDb2() {
    db.reset();
    std::remove(path.string().c_str());
    std::remove((path.string() + "-wal").c_str());
    std::remove((path.string() + "-shm").c_str());
  }
};

// ── Tests ─────────────────────────────────────────────────────────────────────
TEST_CASE("Identical file produces same fast fingerprint", "[hash]") {
  auto p = writeTempFile("same.bin", std::string(128 * 1024, 'A'));  // > 2 chunks
  uint64_t h1 = HashDedup::fastFingerprint(p.string());
  uint64_t h2 = HashDedup::fastFingerprint(p.string());
  REQUIRE(h1 != 0);
  REQUIRE(h1 == h2);
  std::remove(p.string().c_str());
}

TEST_CASE("1-byte change produces different full hash", "[hash]") {
  std::string data(1024, 'B');
  auto p1 = writeTempFile("orig.bin", data);
  data[0] = 'C';
  auto p2 = writeTempFile("mod.bin", data);

  auto h1 = HashDedup::fullHash(p1.string());
  auto h2 = HashDedup::fullHash(p2.string());
  REQUIRE_FALSE(h1.empty());
  REQUIRE_FALSE(h2.empty());
  REQUIRE(h1 != h2);

  std::remove(p1.string().c_str());
  std::remove(p2.string().c_str());
}

TEST_CASE("isDuplicate returns photo_id for known hash", "[hash]") {
  TempDb2 f;
  PhotoRepository repo(*f.db);

  VolumeRecord v;
  v.uuid = "DU1";
  v.label = "L";
  v.mountPath = "/DU1";
  int64_t vid = repo.upsertVolume(v);
  FolderRecord folder;
  folder.volumeId = vid;
  folder.path = "/DU1/A";
  folder.name = "A";
  int64_t fid = repo.upsertFolder(folder);

  PhotoRecord p;
  p.folderId = fid;
  p.filename = "test.jpg";
  p.fileHash = "deadbeef1234567890";
  p.fileSize = 1;
  int64_t pid = repo.insertPhoto(p);

  auto found = HashDedup::isDuplicate(*f.db, "deadbeef1234567890");
  REQUIRE(found.has_value());
  REQUIRE(*found == pid);

  auto notFound = HashDedup::isDuplicate(*f.db, "0000000000000000");
  REQUIRE_FALSE(notFound.has_value());
}

TEST_CASE("Full hash is deterministic and 32 hex chars", "[hash]") {
  auto p = writeTempFile("det.bin", "Hello, World!");
  auto h1 = HashDedup::fullHash(p.string());
  auto h2 = HashDedup::fullHash(p.string());
  REQUIRE(h1.size() == 32);
  REQUIRE(h1 == h2);
  std::remove(p.string().c_str());
}
