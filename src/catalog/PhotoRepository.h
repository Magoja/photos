#pragma once
#include "Database.h"
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <optional>
#include <cstdint>

namespace catalog {

struct PhotoRecord {
  int64_t id = 0;
  int64_t folderId = 0;
  std::string filename;
  std::string fileHash;
  int64_t fileSize = 0;
  std::string importTime;
  std::string captureTime;
  std::string cameraMake;
  std::string cameraModel;
  std::string lensModel;
  double focalLengthMm = 0.0;
  double aperture = 0.0;
  std::string shutterSpeed;
  int iso = 0;
  int widthPx = 0;
  int heightPx = 0;
  double gpsLat = 0.0;
  double gpsLon = 0.0;
  double gpsAltM = 0.0;
  int picked = 0;
  int rating = 0;
  std::string colorLabel;
  std::string thumbPath;
  int thumbWidth = 0;
  int thumbHeight = 0;
  int64_t thumbMtime = 0;
  std::string thumbMicroPath;
  std::string editSettings = "{}";
};

struct FolderRecord {
  int64_t id = 0;
  int64_t parentId = 0;
  int64_t volumeId = 0;
  std::string path;
  std::string name;
};

struct VolumeRecord {
  int64_t id = 0;
  std::string uuid;
  std::string label;
  std::string mountPath;
  std::string lastSeen;
};

class PhotoRepository {
 public:
  explicit PhotoRepository(Database& db) : db_(db) {}

  // ── Volumes ──────────────────────────────────────────────────────────────
  int64_t upsertVolume(const VolumeRecord& v);
  std::optional<VolumeRecord> findVolume(const std::string& uuid);
  std::vector<VolumeRecord> listVolumes();

  // ── Folders ──────────────────────────────────────────────────────────────
  int64_t upsertFolder(const FolderRecord& f);
  std::optional<FolderRecord> findFolder(const std::string& path);
  std::vector<FolderRecord> listFolders(int64_t volumeId = 0);
  int64_t folderPhotoCount(int64_t folderId);
  std::map<int64_t, int64_t> allFolderPhotoCounts();

  // ── Photos ───────────────────────────────────────────────────────────────
  // Returns new photo id; throws DbError on UNIQUE(folder_id,filename) conflict
  int64_t insertPhoto(const PhotoRecord& p);

  std::optional<PhotoRecord> findById(int64_t id);
  std::optional<int64_t> findByHash(const std::string& hash);
  std::string getThumbPath(int64_t photoId);
  std::string getThumbMicroPath(int64_t photoId);

  std::vector<int64_t> queryByFolder(int64_t folderId, bool pickedOnly = false);
  std::vector<int64_t> queryAll(bool pickedOnly = false);

  // Returns {id → {thumbPath, editSettings}} for all photos in a folder.
  // folderId == 0 means all photos. Single SQL query; called once per reload().
  std::unordered_map<int64_t, std::pair<std::string, std::string>>
    queryThumbMeta(int64_t folderId, bool pickedOnly);

  void updatePicked(int64_t id, int picked);
  void updateThumb(int64_t id, const std::string& path, int w, int h, int64_t mtime);
  void updateThumbMicro(int64_t id, const std::string& path);
  void updateEditSettings(int64_t id, const std::string& json);
  void updateEditSettingsBulk(const std::vector<int64_t>& ids, const std::string& json);

  // Clears all thumb_path / thumb_micro_path entries so thumbnails regenerate.
  void clearAllThumbs();

  // ── App settings ─────────────────────────────────────────────────────────
  std::string getSetting(const std::string& key, const std::string& def = "");
  void setSetting(const std::string& key, const std::string& value);

  // ── Library root ─────────────────────────────────────────────────────────
  void setLibraryRoot(const std::string& root);
  std::string libraryRoot() const { return libraryRoot_; }
  std::string fullPathFor(int64_t folderId, const std::string& filename);
  bool libraryRootExists() const;

  // ── Direct DB access (for components that need raw queries) ───────────────
  Database& db() { return db_; }

 private:
  Database& db_;
  std::string libraryRoot_;
  PhotoRecord rowToPhoto(Stmt& s);
};

}  // namespace catalog
