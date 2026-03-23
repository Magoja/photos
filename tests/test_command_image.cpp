#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "command/handlers/ImageAdjustHandler.h"
#include "command/handlers/ImageRevertHandler.h"
#include "command/handlers/ImageCropHandler.h"
#include "command/handlers/ImageSaveHandler.h"
#include "catalog/Database.h"
#include "catalog/Schema.h"
#include "catalog/PhotoRepository.h"
#include "catalog/EditSettings.h"
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;
using namespace catalog;

// ── Fixture ───────────────────────────────────────────────────────────────────

struct TempDb {
  fs::path path;
  std::unique_ptr<Database> db;
  std::unique_ptr<PhotoRepository> repo;

  TempDb() {
    path = fs::temp_directory_path() /
           ("test_cmd_image_" + std::to_string(std::rand()) + ".db");
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

  // Insert a minimal photo and return its id.
  int64_t insertPhoto(const std::string& editSettings = "{}") {
    VolumeRecord v;
    v.uuid = "vol-" + std::to_string(std::rand());
    v.mountPath = "/Volumes/Test";
    const int64_t vid = repo->upsertVolume(v);

    FolderRecord f;
    f.volumeId = vid;
    f.path = "/Volumes/Test/DCIM";
    f.name = "DCIM";
    const int64_t fid = repo->upsertFolder(f);

    PhotoRecord p;
    p.folderId = fid;
    p.filename  = "img_" + std::to_string(std::rand()) + ".arw";
    p.fileHash  = "hash" + std::to_string(std::rand());
    p.editSettings = editSettings;
    const int64_t pid = repo->insertPhoto(p);

    // Write the initial edit_settings (insertPhoto doesn't persist it directly)
    if (editSettings != "{}") {
      repo->updateEditSettings(pid, editSettings);
    }
    return pid;
  }
};

// ── ImageAdjustHandler ────────────────────────────────────────────────────────

TEST_CASE("image.adjust: validate rejects missing id", "[adjust]") {
  TempDb f;
  command::ImageAdjustHandler h(*f.repo);
  REQUIRE_FALSE(h.validate({{"exposure", 1.0}}).has_value());
}

TEST_CASE("image.adjust: validate rejects non-number adjust field", "[adjust]") {
  TempDb f;
  command::ImageAdjustHandler h(*f.repo);
  REQUIRE_FALSE(h.validate({{"id", 1}, {"exposure", "bad"}}).has_value());
}

TEST_CASE("image.adjust: validate passes with id only", "[adjust]") {
  TempDb f;
  command::ImageAdjustHandler h(*f.repo);
  REQUIRE(h.validate({{"id", 1}}).has_value());
}

TEST_CASE("image.adjust: sets exposure on photo", "[adjust]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();
  command::ImageAdjustHandler h(*f.repo);

  const auto result = h.execute({{"id", pid}, {"exposure", 1.5}});

  REQUIRE(result.has_value());
  const auto rec = f.repo->findById(pid);
  REQUIRE(rec.has_value());
  const auto settings = EditSettings::fromJson(rec->editSettings);
  REQUIRE(settings.exposure == Catch::Approx(1.5f));
}

TEST_CASE("image.adjust: partial params leave other fields untouched", "[adjust]") {
  TempDb f;
  // Start with all fields set.
  const std::string initial =
    R"({"exposure":1.0,"temperature":50.0,"contrast":20.0,"saturation":-10.0})";
  const int64_t pid = f.insertPhoto(initial);
  command::ImageAdjustHandler h(*f.repo);

  // Only override exposure.
  h.execute({{"id", pid}, {"exposure", 2.0}});

  const auto rec = f.repo->findById(pid);
  const auto s = EditSettings::fromJson(rec->editSettings);
  REQUIRE(s.exposure    == Catch::Approx(2.0f));
  REQUIRE(s.temperature == Catch::Approx(50.0f));
  REQUIRE(s.contrast    == Catch::Approx(20.0f));
  REQUIRE(s.saturation  == Catch::Approx(-10.0f));
}

TEST_CASE("image.adjust: returns failure for unknown photo id", "[adjust]") {
  TempDb f;
  command::ImageAdjustHandler h(*f.repo);
  const auto result = h.execute({{"id", 99999}});
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().find("99999") != std::string::npos);
}

// ── ImageRevertHandler ────────────────────────────────────────────────────────

TEST_CASE("image.revert: validate rejects missing id", "[revert]") {
  TempDb f;
  command::ImageRevertHandler h(*f.repo);
  REQUIRE_FALSE(h.validate({}).has_value());
}

TEST_CASE("image.revert: clears edit_settings to empty object", "[revert]") {
  TempDb f;
  const std::string initial = R"({"exposure":2.0,"contrast":30.0})";
  const int64_t pid = f.insertPhoto(initial);
  command::ImageRevertHandler h(*f.repo);

  const auto result = h.execute({{"id", pid}});

  REQUIRE(result.has_value());
  const auto rec = f.repo->findById(pid);
  const auto s = EditSettings::fromJson(rec->editSettings);
  REQUIRE(s.exposure == Catch::Approx(0.0f));
  REQUIRE(s.contrast == Catch::Approx(0.0f));
}

TEST_CASE("image.revert: returns failure for unknown photo id", "[revert]") {
  TempDb f;
  command::ImageRevertHandler h(*f.repo);
  const auto result = h.execute({{"id", 99999}});
  REQUIRE_FALSE(result.has_value());
}

// ── Cache-invalidation: ImageAdjustHandler ────────────────────────────────────

TEST_CASE("image.adjust: clears thumb_path in DB on success", "[adjust]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();
  f.repo->updateThumb(pid, "/thumbs/old.jpg", 256, 256, 0);
  REQUIRE(f.repo->findById(pid)->thumbPath == "/thumbs/old.jpg");

  command::ImageAdjustHandler h(*f.repo);
  REQUIRE(h.execute({{"id", pid}, {"exposure", 0.5}}).has_value());

  REQUIRE(f.repo->findById(pid)->thumbPath.empty());
}

TEST_CASE("image.adjust: fires adjustedCb on success", "[adjust]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();

  int callCount = 0;
  int64_t calledWith = -1;
  command::ImageAdjustHandler h(*f.repo, [&](const int64_t id) {
    ++callCount;
    calledWith = id;
  });

  REQUIRE(h.execute({{"id", pid}, {"exposure", 1.0}}).has_value());
  REQUIRE(callCount == 1);
  REQUIRE(calledWith == pid);
}

TEST_CASE("image.adjust: does not fire adjustedCb on unknown photo", "[adjust]") {
  TempDb f;

  int callCount = 0;
  command::ImageAdjustHandler h(*f.repo, [&](int64_t) { ++callCount; });

  REQUIRE_FALSE(h.execute({{"id", 99999}, {"exposure", 1.0}}).has_value());
  REQUIRE(callCount == 0);
}

// ── Cache-invalidation: ImageRevertHandler ────────────────────────────────────

TEST_CASE("image.revert: clears thumb_path in DB on success", "[revert]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();
  f.repo->updateThumb(pid, "/thumbs/old.jpg", 256, 256, 0);
  REQUIRE(f.repo->findById(pid)->thumbPath == "/thumbs/old.jpg");

  command::ImageRevertHandler h(*f.repo);
  REQUIRE(h.execute({{"id", pid}}).has_value());

  REQUIRE(f.repo->findById(pid)->thumbPath.empty());
}

TEST_CASE("image.revert: fires adjustedCb on success", "[revert]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();

  int callCount = 0;
  int64_t calledWith = -1;
  command::ImageRevertHandler h(*f.repo, [&](const int64_t id) {
    ++callCount;
    calledWith = id;
  });

  REQUIRE(h.execute({{"id", pid}}).has_value());
  REQUIRE(callCount == 1);
  REQUIRE(calledWith == pid);
}

TEST_CASE("image.revert: does not fire adjustedCb on unknown photo", "[revert]") {
  TempDb f;

  int callCount = 0;
  command::ImageRevertHandler h(*f.repo, [&](int64_t) { ++callCount; });

  REQUIRE_FALSE(h.execute({{"id", 99999}}).has_value());
  REQUIRE(callCount == 0);
}

// ── ImageCropHandler ──────────────────────────────────────────────────────────

TEST_CASE("image.crop: writes crop fields, leaves adjust fields untouched", "[crop]") {
  TempDb f;
  const std::string initial =
    R"({"exposure":1.5,"temperature":30.0,"contrast":10.0,"saturation":-5.0})";
  const int64_t pid = f.insertPhoto(initial);
  command::ImageCropHandler h(*f.repo);

  const auto result = h.execute({{"id", pid}, {"x", 0.1}, {"y", 0.2}, {"w", 0.8}, {"h", 0.7}});

  REQUIRE(result.has_value());
  const auto rec = f.repo->findById(pid);
  const auto s = EditSettings::fromJson(rec->editSettings);
  REQUIRE(s.crop.x == Catch::Approx(0.1f));
  REQUIRE(s.crop.y == Catch::Approx(0.2f));
  REQUIRE(s.crop.w == Catch::Approx(0.8f));
  REQUIRE(s.crop.h == Catch::Approx(0.7f));
  // Adjust fields must be untouched.
  REQUIRE(s.exposure    == Catch::Approx(1.5f));
  REQUIRE(s.temperature == Catch::Approx(30.0f));
  REQUIRE(s.contrast    == Catch::Approx(10.0f));
  REQUIRE(s.saturation  == Catch::Approx(-5.0f));
}

TEST_CASE("image.crop: partial params leave other crop fields untouched", "[crop]") {
  TempDb f;
  const std::string initial =
    R"({"crop":{"x":0.05,"y":0.05,"w":0.9,"h":0.9,"angle":2.0}})";
  const int64_t pid = f.insertPhoto(initial);
  command::ImageCropHandler h(*f.repo);

  // Only override x and y.
  h.execute({{"id", pid}, {"x", 0.15}, {"y", 0.25}});

  const auto rec = f.repo->findById(pid);
  const auto s = EditSettings::fromJson(rec->editSettings);
  REQUIRE(s.crop.x == Catch::Approx(0.15f));
  REQUIRE(s.crop.y == Catch::Approx(0.25f));
  REQUIRE(s.crop.w == Catch::Approx(0.9f));
  REQUIRE(s.crop.h == Catch::Approx(0.9f));
  REQUIRE(s.crop.angleDeg == Catch::Approx(2.0f));
}

TEST_CASE("image.crop: returns failure for unknown photo id", "[crop]") {
  TempDb f;
  command::ImageCropHandler h(*f.repo);
  const auto result = h.execute({{"id", 99999}, {"x", 0.1}});
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().find("99999") != std::string::npos);
}

// ── ImageSaveHandler ──────────────────────────────────────────────────────────

TEST_CASE("image.save: round-trips full JSON blob", "[save]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();
  command::ImageSaveHandler h(*f.repo, nullptr);

  const nlohmann::json settings = {
    {"exposure", 2.0}, {"temperature", -20.0}, {"contrast", 15.0}, {"saturation", 5.0},
    {"crop", {{"x", 0.1}, {"y", 0.05}, {"w", 0.8}, {"h", 0.9}, {"angle", 3.5}}}
  };
  const auto result = h.execute({{"id", pid}, {"settings", settings}});

  REQUIRE(result.has_value());
  const auto rec = f.repo->findById(pid);
  const auto s = EditSettings::fromJson(rec->editSettings);
  REQUIRE(s.exposure    == Catch::Approx(2.0f));
  REQUIRE(s.temperature == Catch::Approx(-20.0f));
  REQUIRE(s.contrast    == Catch::Approx(15.0f));
  REQUIRE(s.saturation  == Catch::Approx(5.0f));
  REQUIRE(s.crop.x      == Catch::Approx(0.1f));
  REQUIRE(s.crop.y      == Catch::Approx(0.05f));
  REQUIRE(s.crop.w      == Catch::Approx(0.8f));
  REQUIRE(s.crop.h      == Catch::Approx(0.9f));
  REQUIRE(s.crop.angleDeg == Catch::Approx(3.5f));
}

TEST_CASE("image.save: fires savedCb with correct id", "[save]") {
  TempDb f;
  const int64_t pid = f.insertPhoto();

  int callCount = 0;
  int64_t calledWith = -1;
  command::ImageSaveHandler h(*f.repo, [&](const int64_t id) {
    ++callCount;
    calledWith = id;
  });

  REQUIRE(h.execute({{"id", pid}, {"settings", nlohmann::json::object()}}).has_value());
  REQUIRE(callCount == 1);
  REQUIRE(calledWith == pid);
}

TEST_CASE("image.save: returns failure for unknown photo id", "[save]") {
  TempDb f;

  int callCount = 0;
  command::ImageSaveHandler h(*f.repo, [&](int64_t) { ++callCount; });

  const auto result = h.execute({{"id", 99999}, {"settings", nlohmann::json::object()}});
  REQUIRE_FALSE(result.has_value());
  REQUIRE(callCount == 0);
}
