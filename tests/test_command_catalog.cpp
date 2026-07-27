#include <catch2/catch_test_macros.hpp>
#include "command/handlers/CatalogPickHandler.h"
#include "command/handlers/CatalogOpenHandler.h"
#include "command/handlers/CatalogDeletePhotosHandler.h"
#include "command/handlers/CatalogDeleteFolderHandler.h"
#include "catalog/Database.h"
#include "catalog/Schema.h"
#include "catalog/PhotoRepository.h"
#include <filesystem>
#include <fstream>
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
    path = fs::temp_directory_path() / ("test_cmd_catalog_" + std::to_string(std::rand()) + ".db");
    db = std::make_unique<Database>(path.string());
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
    p.filename = "img_" + std::to_string(std::rand()) + ".arw";
    p.fileHash = "hash" + std::to_string(std::rand());
    return repo->insertPhoto(p);
  }

  int64_t insertFolder(const std::string& path, int64_t parentId = 0) {
    FolderRecord f;
    f.parentId = parentId;
    f.path = path;
    f.name = path;
    return repo->upsertFolder(f);
  }

  int64_t insertPhotoIn(int64_t folderId, const std::string& hash,
                        const std::string& thumbPath = "") {
    PhotoRecord p;
    p.folderId = folderId;
    p.filename = "img_" + std::to_string(std::rand()) + ".arw";
    p.fileHash = hash;
    const int64_t pid = repo->insertPhoto(p);
    if (!thumbPath.empty()) {
      repo->updateThumb(pid, thumbPath, 256, 256, 0);
    }
    return pid;
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
    calledId = id;
    calledPicked = picked;
  });

  REQUIRE(h.execute({{"id", pid}, {"picked", 1}}).has_value());
  REQUIRE(callCount == 1);
  REQUIRE(calledId == pid);
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
  REQUIRE(callCount == 1);
  REQUIRE(calledWith == 42);
}

TEST_CASE("catalog.photo.open: does not access DB", "[open]") {
  // No repo needed — openHandler takes no repo
  int callCount = 0;
  command::CatalogOpenHandler h([&](int64_t) { ++callCount; });
  REQUIRE(h.execute({{"id", 1}}).has_value());
  REQUIRE(callCount == 1);
}

// ── CatalogDeletePhotosHandler ────────────────────────────────────────────────

TEST_CASE("catalog.delete.photos: validate rejects missing/empty/non-int ids", "[delete]") {
  TempDb f;
  command::CatalogDeletePhotosHandler h(*f.repo);
  REQUIRE_FALSE(h.validate({}).has_value());
  REQUIRE_FALSE(h.validate({{"ids", nlohmann::json::array()}}).has_value());
  REQUIRE_FALSE(h.validate({{"ids", {1, "two", 3}}}).has_value());
  REQUIRE(h.validate({{"ids", {1, 2, 3}}}).has_value());
}

TEST_CASE("catalog.delete.photos: removes selected rows, leaves others", "[delete]") {
  TempDb f;
  const int64_t fid = f.insertFolder("/lib/A");
  const int64_t keep = f.insertPhotoIn(fid, "h-keep");
  const int64_t del1 = f.insertPhotoIn(fid, "h-del1");
  const int64_t del2 = f.insertPhotoIn(fid, "h-del2");

  command::CatalogDeletePhotosHandler h(*f.repo);
  const auto res = h.execute({{"ids", {del1, del2}}});
  REQUIRE(res.has_value());

  const auto remaining = f.repo->queryByFolder(fid);
  REQUIRE(remaining.size() == 1);
  REQUIRE(remaining[0] == keep);
  REQUIRE(res->at("deleted").size() == 2);
}

TEST_CASE("catalog.delete.photos: shared-hash thumbnail kept until last dupe gone", "[delete]") {
  TempDb f;
  const int64_t fid = f.insertFolder("/lib/B");

  const fs::path thumb =
    fs::temp_directory_path() / ("test_thumb_" + std::to_string(std::rand()) + ".jpg");
  std::ofstream(thumb.string()) << "jpegbytes";
  REQUIRE(fs::exists(thumb));

  // Two photos share one content hash → share one thumbnail file.
  const int64_t a = f.insertPhotoIn(fid, "dup-hash", thumb.string());
  const int64_t b = f.insertPhotoIn(fid, "dup-hash", thumb.string());

  command::CatalogDeletePhotosHandler h(*f.repo);

  // Deleting one leaves the surviving dupe → thumbnail file must remain.
  REQUIRE(h.execute({{"ids", {a}}}).has_value());
  REQUIRE(fs::exists(thumb));

  // Deleting the last reference removes the thumbnail file.
  REQUIRE(h.execute({{"ids", {b}}}).has_value());
  REQUIRE_FALSE(fs::exists(thumb));
}

// ── CatalogDeleteFolderHandler ────────────────────────────────────────────────

TEST_CASE("catalog.delete.folder: validate rejects missing or non-positive id", "[delete]") {
  TempDb f;
  command::CatalogDeleteFolderHandler h(*f.repo);
  REQUIRE_FALSE(h.validate({}).has_value());
  REQUIRE_FALSE(h.validate({{"folderId", 0}}).has_value());
  REQUIRE_FALSE(h.validate({{"folderId", -5}}).has_value());
  REQUIRE(h.validate({{"folderId", 1}}).has_value());
}

TEST_CASE("catalog.delete.folder: cascades to child folders and their photos", "[delete]") {
  TempDb f;
  const int64_t parent = f.insertFolder("/lib/parent");
  const int64_t child = f.insertFolder("/lib/parent/child", parent);
  f.insertPhotoIn(parent, "p1");
  f.insertPhotoIn(parent, "p2");
  f.insertPhotoIn(child, "c1");

  command::CatalogDeleteFolderHandler h(*f.repo);
  const auto res = h.execute({{"folderId", parent}});
  REQUIRE(res.has_value());
  REQUIRE(res->at("deleted").size() == 3);

  // Both folder rows and all photos are gone.
  REQUIRE(f.repo->queryByFolder(parent).empty());
  REQUIRE(f.repo->queryByFolder(child).empty());
  const auto folders = f.repo->listFolders();
  for (const auto& fr : folders) {
    REQUIRE(fr.id != parent);
    REQUIRE(fr.id != child);
  }
}
