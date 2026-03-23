#include <catch2/catch_test_macros.hpp>
#include "command/handlers/CatalogPickHandler.h"
#include "command/handlers/CatalogOpenHandler.h"
#include "catalog/Database.h"
#include "catalog/Schema.h"
#include "catalog/PhotoRepository.h"
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;
using namespace catalog;

namespace {

struct TempDb {
  fs::path path;
  std::unique_ptr<Database> db;
  std::unique_ptr<PhotoRepository> repo;
  int64_t lastFolderId = 0;

  TempDb() {
    path = fs::temp_directory_path() /
           ("test_cmd_catalog_" + std::to_string(std::rand()) + ".db");
    db   = std::make_unique<Database>(path.string());
    Schema::apply(*db);
    repo = std::make_unique<PhotoRepository>(*db);
  }
  ~TempDb() {
    repo.reset();
    db.reset();
    std::remove(path.string().c_str());
    std::remove((path.string() + "-wal").c_str());
    std::remove((path.string() + "-shm").c_str());
  }

  int64_t insertPhoto() {
    VolumeRecord v;
    v.uuid = "vol-" + std::to_string(std::rand());
    v.mountPath = "/Volumes/Test";
    const int64_t vid = repo->upsertVolume(v);

    FolderRecord f;
    f.volumeId = vid;
    f.path = "/Volumes/Test/DCIM";
    f.name = "DCIM";
    const int64_t fid = repo->upsertFolder(f);
    lastFolderId = fid;

    PhotoRecord p;
    p.folderId = fid;
    p.filename  = "img_" + std::to_string(std::rand()) + ".arw";
    p.fileHash  = "hash" + std::to_string(std::rand());
    return repo->insertPhoto(p);
  }
};

}  // namespace

// ── CatalogPickHandler ────────────────────────────────────────────────────────

TEST_CASE("catalog.pick: validate rejects missing id", "[pick]") {
  TempDb f;
  command::CatalogPickHandler h(*f.repo, nullptr);
  REQUIRE_FALSE(h.validate({{"picked", 1}}).has_value());
}

TEST_CASE("catalog.pick: validate rejects missing picked", "[pick]") {
  TempDb f;
  command::CatalogPickHandler h(*f.repo, nullptr);
  REQUIRE_FALSE(h.validate({{"id", 1}}).has_value());
}

TEST_CASE("catalog.pick: updates picked column in DB", "[pick]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();
  const int64_t fid = f.lastFolderId;

  // Verify unpicked before
  REQUIRE(f.repo->queryByFolder(fid, /*pickedOnly=*/true).empty());

  command::CatalogPickHandler h(*f.repo, nullptr);
  const auto result = h.execute({{"id", pid}, {"picked", 1}});

  REQUIRE(result.has_value());
  const auto picked = f.repo->queryByFolder(fid, /*pickedOnly=*/true);
  REQUIRE(picked.size() == 1);
  REQUIRE(picked[0] == pid);
}

TEST_CASE("catalog.pick: callback fired with correct args", "[pick]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();

  int callCount = 0;
  int64_t calledId = -1;
  int calledPicked = -1;
  command::CatalogPickHandler h(*f.repo, [&](const int64_t id, const int picked) {
    ++callCount;
    calledId     = id;
    calledPicked = picked;
  });

  REQUIRE(h.execute({{"id", pid}, {"picked", 1}}).has_value());
  REQUIRE(callCount   == 1);
  REQUIRE(calledId    == pid);
  REQUIRE(calledPicked == 1);
}

TEST_CASE("catalog.pick: returns failure for unknown photo id", "[pick]") {
  TempDb f;
  command::CatalogPickHandler h(*f.repo, nullptr);
  const auto result = h.execute({{"id", 99999}, {"picked", 1}});
  REQUIRE_FALSE(result.has_value());
}

// ── CatalogOpenHandler ────────────────────────────────────────────────────────

TEST_CASE("catalog.photo.open: validate rejects missing id", "[open]") {
  command::CatalogOpenHandler h(nullptr);
  REQUIRE_FALSE(h.validate({}).has_value());
}

TEST_CASE("catalog.photo.open: fires selectCb with correct id", "[open]") {
  int callCount = 0;
  int64_t calledWith = -1;
  command::CatalogOpenHandler h([&](const int64_t id) {
    ++callCount;
    calledWith = id;
  });

  const auto result = h.execute({{"id", 42}});
  REQUIRE(result.has_value());
  REQUIRE(callCount  == 1);
  REQUIRE(calledWith == 42);
}

TEST_CASE("catalog.photo.open: does not access DB", "[open]") {
  // No repo needed — openHandler takes no repo
  int callCount = 0;
  command::CatalogOpenHandler h([&](int64_t) { ++callCount; });
  REQUIRE(h.execute({{"id", 1}}).has_value());
  REQUIRE(callCount == 1);
}
