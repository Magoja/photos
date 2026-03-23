#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "command/handlers/MetaSyncHandler.h"
#include "catalog/Database.h"
#include "catalog/Schema.h"
#include "catalog/PhotoRepository.h"
#include "catalog/EditSettings.h"
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;
using namespace catalog;

namespace {

struct TempDb {
  fs::path path;
  std::unique_ptr<Database> db;
  std::unique_ptr<PhotoRepository> repo;

  TempDb() {
    path = fs::temp_directory_path() /
           ("test_cmd_meta_" + std::to_string(std::rand()) + ".db");
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

  int64_t insertPhoto(const std::string& editSettings = "{}") {
    VolumeRecord v;
    v.uuid = "vol-" + std::to_string(std::rand());
    v.mountPath = "/Volumes/Test";
    const int64_t vid = repo->upsertVolume(v);

    FolderRecord f;
    f.volumeId  = vid;
    f.path      = "/Volumes/Test/DCIM";
    f.name      = "DCIM";
    const int64_t fid = repo->upsertFolder(f);

    PhotoRecord p;
    p.folderId = fid;
    p.filename  = "img_" + std::to_string(std::rand()) + ".arw";
    p.fileHash  = "hash" + std::to_string(std::rand());
    const int64_t pid = repo->insertPhoto(p);
    if (editSettings != "{}") {
      repo->updateEditSettings(pid, editSettings);
    }
    return pid;
  }
};

}  // namespace

// ── MetaSyncHandler ───────────────────────────────────────────────────────────

TEST_CASE("metasync.apply: validate rejects missing primaryId", "[metasync]") {
  TempDb f;
  command::MetaSyncHandler h(*f.repo, nullptr);
  REQUIRE_FALSE(h.validate({{"targetIds", nlohmann::json::array()}}).has_value());
}

TEST_CASE("metasync.apply: validate rejects missing targetIds", "[metasync]") {
  TempDb f;
  command::MetaSyncHandler h(*f.repo, nullptr);
  REQUIRE_FALSE(h.validate({{"primaryId", 1}}).has_value());
}

TEST_CASE("metasync.apply: syncAdjust propagates exposure, crop unchanged", "[metasync]") {
  TempDb f;
  const std::string srcJson =
    R"({"exposure":2.0,"temperature":30.0,"crop":{"x":0.1,"y":0.1,"w":0.8,"h":0.8,"angle":5.0}})";
  const std::string tgtJson =
    R"({"exposure":0.0,"temperature":0.0,"crop":{"x":0.2,"y":0.2,"w":0.6,"h":0.6,"angle":0.0}})";

  const int64_t primary = f.insertPhoto(srcJson);
  const int64_t target  = f.insertPhoto(tgtJson);

  command::MetaSyncHandler h(*f.repo, nullptr);
  const auto result = h.execute({
      {"primaryId",  primary},
      {"targetIds",  nlohmann::json::array({target})},
      {"syncAdjust", true},
      {"syncCrop",   false}
  });

  REQUIRE(result.has_value());
  const auto rec = f.repo->findById(target);
  const auto s = EditSettings::fromJson(rec->editSettings);
  // Adjust fields copied from primary
  REQUIRE(s.exposure    == Catch::Approx(2.0f));
  REQUIRE(s.temperature == Catch::Approx(30.0f));
  // Crop unchanged from target
  REQUIRE(s.crop.x == Catch::Approx(0.2f));
  REQUIRE(s.crop.y == Catch::Approx(0.2f));
  REQUIRE(s.crop.w == Catch::Approx(0.6f));
  REQUIRE(s.crop.h == Catch::Approx(0.6f));
}

TEST_CASE("metasync.apply: syncCrop propagates crop, adjust unchanged", "[metasync]") {
  TempDb f;
  const std::string srcJson =
    R"({"exposure":1.5,"crop":{"x":0.05,"y":0.05,"w":0.9,"h":0.9,"angle":2.0}})";
  const std::string tgtJson =
    R"({"exposure":0.5,"crop":{"x":0.3,"y":0.3,"w":0.4,"h":0.4,"angle":0.0}})";

  const int64_t primary = f.insertPhoto(srcJson);
  const int64_t target  = f.insertPhoto(tgtJson);

  command::MetaSyncHandler h(*f.repo, nullptr);
  REQUIRE(h.execute({
      {"primaryId",  primary},
      {"targetIds",  nlohmann::json::array({target})},
      {"syncAdjust", false},
      {"syncCrop",   true}
  }).has_value());

  const auto s = EditSettings::fromJson(f.repo->findById(target)->editSettings);
  // Crop copied from primary
  REQUIRE(s.crop.x      == Catch::Approx(0.05f));
  REQUIRE(s.crop.w      == Catch::Approx(0.9f));
  REQUIRE(s.crop.angleDeg == Catch::Approx(2.0f));
  // Adjust unchanged from target
  REQUIRE(s.exposure == Catch::Approx(0.5f));
}

TEST_CASE("metasync.apply: syncAdjust and syncCrop both propagate all fields", "[metasync]") {
  TempDb f;
  const std::string srcJson =
    R"({"exposure":2.5,"contrast":20.0,"crop":{"x":0.1,"y":0.1,"w":0.8,"h":0.8,"angle":3.0}})";
  const int64_t primary = f.insertPhoto(srcJson);
  const int64_t target  = f.insertPhoto();

  command::MetaSyncHandler h(*f.repo, nullptr);
  REQUIRE(h.execute({
      {"primaryId",  primary},
      {"targetIds",  nlohmann::json::array({target})},
      {"syncAdjust", true},
      {"syncCrop",   true}
  }).has_value());

  const auto s = EditSettings::fromJson(f.repo->findById(target)->editSettings);
  REQUIRE(s.exposure    == Catch::Approx(2.5f));
  REQUIRE(s.contrast    == Catch::Approx(20.0f));
  REQUIRE(s.crop.x      == Catch::Approx(0.1f));
  REQUIRE(s.crop.angleDeg == Catch::Approx(3.0f));
}

TEST_CASE("metasync.apply: doneCb fires exactly once", "[metasync]") {
  TempDb f;
  const int64_t primary = f.insertPhoto();
  const int64_t target  = f.insertPhoto();

  int callCount = 0;
  command::MetaSyncHandler h(*f.repo, [&]() { ++callCount; });
  REQUIRE(h.execute({
      {"primaryId",  primary},
      {"targetIds",  nlohmann::json::array({target})},
      {"syncAdjust", true},
      {"syncCrop",   false}
  }).has_value());

  REQUIRE(callCount == 1);
}

TEST_CASE("metasync.apply: primary excluded from targets list", "[metasync]") {
  TempDb f;
  const std::string srcJson = R"({"exposure":3.0})";
  const int64_t primary = f.insertPhoto(srcJson);

  // Include primary in targetIds — it should be skipped
  command::MetaSyncHandler h(*f.repo, nullptr);
  REQUIRE(h.execute({
      {"primaryId",  primary},
      {"targetIds",  nlohmann::json::array({primary})},
      {"syncAdjust", true},
      {"syncCrop",   false}
  }).has_value());

  // Primary's settings should be unchanged (we didn't try to merge primary into itself)
  const auto s = EditSettings::fromJson(f.repo->findById(primary)->editSettings);
  REQUIRE(s.exposure == Catch::Approx(3.0f));
}
