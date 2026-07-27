#include "DebugReimportMetadataHandler.h"

#include "import/RawDecoder.h"

#include <spdlog/spdlog.h>
#include <filesystem>

namespace command {

namespace {

// Copy the EXIF/GPS fields decoded from a source file into a PhotoRecord shape
// that PhotoRepository::updateMetadata consumes.
catalog::PhotoRecord recordFromExif(const import_ns::ExifData& ex) {
  catalog::PhotoRecord p;
  p.captureTime = ex.captureTime;
  p.cameraMake = ex.cameraMake;
  p.cameraModel = ex.cameraModel;
  p.lensModel = ex.lensModel;
  p.focalLengthMm = ex.focalLengthMm;
  p.aperture = ex.aperture;
  p.shutterSpeed = ex.shutterSpeed;
  p.iso = ex.iso;
  p.widthPx = ex.widthPx;
  p.heightPx = ex.heightPx;
  p.gpsLat = ex.gpsLat;
  p.gpsLon = ex.gpsLon;
  p.gpsAltM = ex.gpsAltM;
  return p;
}

}  // namespace

ValidationResult DebugReimportMetadataHandler::validate(const nlohmann::json& /*params*/) const {
  return valid();  // no params
}

CommandResult DebugReimportMetadataHandler::execute(nlohmann::json /*params*/) {
  const std::vector<int64_t> ids = repo_.queryAll();
  int updated = 0;
  int errors = 0;

  for (const int64_t id : ids) {
    const auto rec = repo_.findById(id);
    if (!rec) {
      ++errors;
      continue;
    }
    const std::string srcPath = repo_.fullPathFor(rec->folderId, rec->filename);
    std::error_code ec;
    if (srcPath.empty() || !std::filesystem::exists(srcPath, ec)) {
      ++errors;
      continue;
    }
    const auto exif = import_ns::RawDecoder::decodeMetadata(srcPath);
    repo_.updateMetadata(id, recordFromExif(exif));
    ++updated;
  }

  spdlog::info("debug.reimport.metadata: {} updated, {} errors, {} total", updated, errors,
               ids.size());
  return success({{"updated", updated}, {"errors", errors}, {"total", ids.size()}});
}

}  // namespace command
