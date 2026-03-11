#include <catch2/catch_test_macros.hpp>
#include "catalog/Database.h"
#include "catalog/Schema.h"
#include "catalog/PhotoRepository.h"
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;
using namespace catalog;

// ── Fixture ───────────────────────────────────────────────────────────────────
struct TempDb {
  fs::path path;
  std::unique_ptr<Database> db;

  TempDb() {
    path = fs::temp_directory_path() / ("test_photo_" + std::to_string(std::rand()) + ".db");
    db = std::make_unique<Database>(path.string());
    Schema::apply(*db);
  }
  ~TempDb() {
    db.reset();
    std::remove(path.string().c_str());
    std::remove((path.string() + "-wal").c_str());
    std::remove((path.string() + "-shm").c_str());
  }
};

// ── Schema & indexes ──────────────────────────────────────────────────────────
TEST_CASE("Schema applies without error", "[db]") {
  TempDb f;
  REQUIRE_NOTHROW(Schema::apply(*f.db));  // idempotent second call
}

TEST_CASE("idx_photos_folder is used by folder query", "[db]") {
  TempDb f;
  // EXPLAIN QUERY PLAN should show USING INDEX idx_photos_folder
  auto s = f.db->prepare(
    "EXPLAIN QUERY PLAN "
    "SELECT id FROM photos WHERE folder_id=1 ORDER BY COALESCE(capture_time,import_time)");
  bool foundIndex = false;
  while (s.step()) {
    std::string detail = s.getText(3);  // "detail" column
    if (detail.find("idx_photos_folder") != std::string::npos) {
      foundIndex = true;
    }
  }
  REQUIRE(foundIndex);
}

// ── Volume & folder CRUD ──────────────────────────────────────────────────────
TEST_CASE("Upsert volume and folder", "[db]") {
  TempDb f;
  PhotoRepository repo(*f.db);

  VolumeRecord v;
  v.uuid = "TEST-UUID-1234";
  v.label = "TestDrive";
  v.mountPath = "/Volumes/Test";
  int64_t vid = repo.upsertVolume(v);
  REQUIRE(vid > 0);

  // idempotent
  int64_t vid2 = repo.upsertVolume(v);
  REQUIRE(vid == vid2);

  FolderRecord folder;
  folder.volumeId = vid;
  folder.path = "/Volumes/Test/DCIM";
  folder.name = "DCIM";
  int64_t fid = repo.upsertFolder(folder);
  REQUIRE(fid > 0);

  auto found = repo.findFolder("/Volumes/Test/DCIM");
  REQUIRE(found.has_value());
  REQUIRE(found->volumeId == vid);
}

// ── Photo insert & dedup ──────────────────────────────────────────────────────
TEST_CASE("Insert photo and find by hash", "[db]") {
  TempDb f;
  PhotoRepository repo(*f.db);

  VolumeRecord v;
  v.uuid = "U1";
  v.label = "L";
  v.mountPath = "/V";
  int64_t vid = repo.upsertVolume(v);
  FolderRecord folder;
  folder.volumeId = vid;
  folder.path = "/V/DCIM";
  folder.name = "DCIM";
  int64_t fid = repo.upsertFolder(folder);

  PhotoRecord p;
  p.folderId = fid;
  p.filename = "IMG_001.CR3";
  p.fileHash = "aabbccdd1122";
  p.fileSize = 12345;
  p.cameraMake = "Canon";

  int64_t pid = repo.insertPhoto(p);
  REQUIRE(pid > 0);

  auto found = repo.findByHash("aabbccdd1122");
  REQUIRE(found.has_value());
  REQUIRE(*found == pid);

  auto notFound = repo.findByHash("doesnotexist");
  REQUIRE_FALSE(notFound.has_value());
}

TEST_CASE("Duplicate (folder_id, filename) is rejected", "[db]") {
  TempDb f;
  PhotoRepository repo(*f.db);

  VolumeRecord v;
  v.uuid = "U2";
  v.label = "L2";
  v.mountPath = "/V2";
  int64_t vid = repo.upsertVolume(v);
  FolderRecord folder;
  folder.volumeId = vid;
  folder.path = "/V2/DCIM";
  folder.name = "DCIM";
  int64_t fid = repo.upsertFolder(folder);

  PhotoRecord p;
  p.folderId = fid;
  p.filename = "IMG_002.CR3";
  p.fileSize = 1;

  REQUIRE_NOTHROW(repo.insertPhoto(p));
  REQUIRE_THROWS_AS(repo.insertPhoto(p), DbError);
}

// ── queryByFolder ─────────────────────────────────────────────────────────────
TEST_CASE("queryByFolder returns correct IDs", "[db]") {
  TempDb f;
  PhotoRepository repo(*f.db);

  VolumeRecord v;
  v.uuid = "U3";
  v.label = "L3";
  v.mountPath = "/V3";
  int64_t vid = repo.upsertVolume(v);
  FolderRecord folder;
  folder.volumeId = vid;
  folder.path = "/V3/A";
  folder.name = "A";
  int64_t fid = repo.upsertFolder(folder);

  for (int i = 0; i < 5; ++i) {
    PhotoRecord p;
    p.folderId = fid;
    p.filename = "file" + std::to_string(i) + ".jpg";
    p.fileSize = i + 1;
    repo.insertPhoto(p);
  }

  auto ids = repo.queryByFolder(fid);
  REQUIRE(ids.size() == 5);
}

// ── picked toggle ─────────────────────────────────────────────────────────────
TEST_CASE("updatePicked and filter", "[db]") {
  TempDb f;
  PhotoRepository repo(*f.db);

  VolumeRecord v;
  v.uuid = "U4";
  v.label = "L4";
  v.mountPath = "/V4";
  int64_t vid = repo.upsertVolume(v);
  FolderRecord folder;
  folder.volumeId = vid;
  folder.path = "/V4/B";
  folder.name = "B";
  int64_t fid = repo.upsertFolder(folder);

  PhotoRecord p;
  p.folderId = fid;
  p.filename = "a.jpg";
  p.fileSize = 1;
  int64_t pid = repo.insertPhoto(p);

  auto all = repo.queryByFolder(fid, false);
  auto picked = repo.queryByFolder(fid, true);
  REQUIRE(all.size() == 1);
  REQUIRE(picked.size() == 0);

  repo.updatePicked(pid, 1);
  auto picked2 = repo.queryByFolder(fid, true);
  REQUIRE(picked2.size() == 1);
  REQUIRE(picked2[0] == pid);
}

// ── App settings ──────────────────────────────────────────────────────────────
TEST_CASE("App settings round-trip", "[db]") {
  TempDb f;
  PhotoRepository repo(*f.db);

  REQUIRE(repo.getSetting("missing_key", "default") == "default");
  repo.setSetting("mykey", "myvalue");
  REQUIRE(repo.getSetting("mykey") == "myvalue");
  repo.setSetting("mykey", "updated");
  REQUIRE(repo.getSetting("mykey") == "updated");
}

// ── Export presets seeded ─────────────────────────────────────────────────────
TEST_CASE("Export presets seeded by Schema", "[db]") {
  TempDb f;
  auto s = f.db->prepare("SELECT COUNT(*) FROM export_presets WHERE name='Medium Quality'");
  REQUIRE(s.step());
  REQUIRE(s.getInt64(0) == 1);
}
