#include "PhotoRepository.h"
#include <spdlog/spdlog.h>

namespace catalog {

PhotoRecord PhotoRepository::rowToPhoto(Stmt& s) {
    PhotoRecord p;
    int c = 0;
    p.id            = s.getInt64(c++);
    p.folderId      = s.getInt64(c++);
    p.filename      = s.getText(c++);
    p.fileHash      = s.getText(c++);
    p.fileSize      = s.getInt64(c++);
    p.importTime    = s.getText(c++);
    p.captureTime   = s.getText(c++);
    p.cameraMake    = s.getText(c++);
    p.cameraModel   = s.getText(c++);
    p.lensModel     = s.getText(c++);
    p.focalLengthMm = s.getDouble(c++);
    p.aperture      = s.getDouble(c++);
    p.shutterSpeed  = s.getText(c++);
    p.iso           = s.getInt(c++);
    p.widthPx       = s.getInt(c++);
    p.heightPx      = s.getInt(c++);
    p.gpsLat        = s.getDouble(c++);
    p.gpsLon        = s.getDouble(c++);
    p.gpsAltM       = s.getDouble(c++);
    p.picked        = s.getInt(c++);
    p.rating        = s.getInt(c++);
    p.colorLabel    = s.getText(c++);
    p.thumbPath     = s.getText(c++);
    p.thumbWidth    = s.getInt(c++);
    p.thumbHeight   = s.getInt(c++);
    p.thumbMtime    = s.getInt64(c++);
    p.editSettings  = s.getText(c++);
    return p;
}

// ── Volumes ───────────────────────────────────────────────────────────────────
int64_t PhotoRepository::upsertVolume(const VolumeRecord& v) {
    auto s = db_.prepare(
        "INSERT INTO volumes(uuid,label,mount_path,last_seen) VALUES(?,?,?,datetime('now'))"
        " ON CONFLICT(uuid) DO UPDATE SET label=excluded.label,"
        " mount_path=excluded.mount_path, last_seen=excluded.last_seen"
        " RETURNING id");
    s.bind(1, v.uuid);
    s.bind(2, v.label);
    s.bind(3, v.mountPath);
    if (s.step()) return s.getInt64(0);
    // Fallback: query id
    auto q = db_.prepare("SELECT id FROM volumes WHERE uuid=?");
    q.bind(1, v.uuid);
    if (q.step()) return q.getInt64(0);
    return 0;
}

std::optional<VolumeRecord> PhotoRepository::findVolume(const std::string& uuid) {
    auto s = db_.prepare("SELECT id,uuid,label,mount_path,last_seen FROM volumes WHERE uuid=?");
    s.bind(1, uuid);
    if (!s.step()) return std::nullopt;
    VolumeRecord v;
    v.id        = s.getInt64(0);
    v.uuid      = s.getText(1);
    v.label     = s.getText(2);
    v.mountPath = s.getText(3);
    v.lastSeen  = s.getText(4);
    return v;
}

// ── Folders ───────────────────────────────────────────────────────────────────
int64_t PhotoRepository::upsertFolder(const FolderRecord& f) {
    auto s = db_.prepare(
        "INSERT INTO folders(parent_id,volume_id,path,name) VALUES(?,?,?,?)"
        " ON CONFLICT(path) DO UPDATE SET volume_id=excluded.volume_id,"
        " parent_id=excluded.parent_id, name=excluded.name"
        " RETURNING id");
    if (f.parentId) s.bind(1, f.parentId); else s.bindNull(1);
    if (f.volumeId) s.bind(2, f.volumeId); else s.bindNull(2);
    s.bind(3, f.path);
    s.bind(4, f.name);
    if (s.step()) return s.getInt64(0);
    auto q = db_.prepare("SELECT id FROM folders WHERE path=?");
    q.bind(1, f.path);
    if (q.step()) return q.getInt64(0);
    return 0;
}

std::optional<FolderRecord> PhotoRepository::findFolder(const std::string& path) {
    auto s = db_.prepare(
        "SELECT id,COALESCE(parent_id,0),COALESCE(volume_id,0),path,name"
        " FROM folders WHERE path=?");
    s.bind(1, path);
    if (!s.step()) return std::nullopt;
    FolderRecord f;
    f.id       = s.getInt64(0);
    f.parentId = s.getInt64(1);
    f.volumeId = s.getInt64(2);
    f.path     = s.getText(3);
    f.name     = s.getText(4);
    return f;
}

std::vector<FolderRecord> PhotoRepository::listFolders(int64_t volumeId) {
    std::vector<FolderRecord> out;
    Stmt s = volumeId
        ? db_.prepare("SELECT id,COALESCE(parent_id,0),COALESCE(volume_id,0),path,name"
                      " FROM folders WHERE volume_id=? ORDER BY path")
        : db_.prepare("SELECT id,COALESCE(parent_id,0),COALESCE(volume_id,0),path,name"
                      " FROM folders ORDER BY path");
    if (volumeId) s.bind(1, volumeId);
    while (s.step()) {
        FolderRecord f;
        f.id       = s.getInt64(0);
        f.parentId = s.getInt64(1);
        f.volumeId = s.getInt64(2);
        f.path     = s.getText(3);
        f.name     = s.getText(4);
        out.push_back(f);
    }
    return out;
}

int64_t PhotoRepository::folderPhotoCount(int64_t folderId) {
    auto s = db_.prepare("SELECT COUNT(*) FROM photos WHERE folder_id=?");
    s.bind(1, folderId);
    if (s.step()) return s.getInt64(0);
    return 0;
}

// ── Photos ────────────────────────────────────────────────────────────────────
int64_t PhotoRepository::insertPhoto(const PhotoRecord& p) {
    static const std::string sql =
        "INSERT INTO photos("
        "folder_id,filename,file_hash,file_size,capture_time,"
        "camera_make,camera_model,lens_model,focal_length_mm,aperture,"
        "shutter_speed,iso,width_px,height_px,edit_settings)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    auto s = db_.prepare(sql);
    s.bind(1,  p.folderId);
    s.bind(2,  p.filename);
    if (!p.fileHash.empty()) s.bind(3, p.fileHash); else s.bindNull(3);
    s.bind(4,  p.fileSize);
    if (!p.captureTime.empty()) s.bind(5, p.captureTime); else s.bindNull(5);
    if (!p.cameraMake.empty())  s.bind(6, p.cameraMake);  else s.bindNull(6);
    if (!p.cameraModel.empty()) s.bind(7, p.cameraModel); else s.bindNull(7);
    if (!p.lensModel.empty())   s.bind(8, p.lensModel);   else s.bindNull(8);
    if (p.focalLengthMm) s.bind(9, p.focalLengthMm); else s.bindNull(9);
    if (p.aperture)      s.bind(10, p.aperture);      else s.bindNull(10);
    if (!p.shutterSpeed.empty()) s.bind(11, p.shutterSpeed); else s.bindNull(11);
    if (p.iso)      s.bind(12, p.iso);      else s.bindNull(12);
    if (p.widthPx)  s.bind(13, p.widthPx);  else s.bindNull(13);
    if (p.heightPx) s.bind(14, p.heightPx); else s.bindNull(14);
    s.bind(15, p.editSettings.empty() ? "{}" : p.editSettings);

    s.step(); // DONE (not ROW)
    return db_.lastInsertRowid();
}

std::optional<PhotoRecord> PhotoRepository::findById(int64_t id) {
    auto s = db_.prepare(
        "SELECT id,folder_id,filename,COALESCE(file_hash,''),file_size,import_time,"
        "COALESCE(capture_time,''),COALESCE(camera_make,''),COALESCE(camera_model,''),"
        "COALESCE(lens_model,''),COALESCE(focal_length_mm,0),COALESCE(aperture,0),"
        "COALESCE(shutter_speed,''),COALESCE(iso,0),COALESCE(width_px,0),COALESCE(height_px,0),"
        "COALESCE(gps_lat,0),COALESCE(gps_lon,0),COALESCE(gps_alt_m,0),"
        "picked,rating,COALESCE(color_label,''),COALESCE(thumb_path,''),"
        "COALESCE(thumb_width,0),COALESCE(thumb_height,0),COALESCE(thumb_mtime,0),"
        "COALESCE(edit_settings,'{}')"
        " FROM photos WHERE id=?");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return rowToPhoto(s);
}

std::optional<int64_t> PhotoRepository::findByHash(const std::string& hash) {
    auto s = db_.prepare("SELECT id FROM photos WHERE file_hash=? LIMIT 1");
    s.bind(1, hash);
    if (s.step()) return s.getInt64(0);
    return std::nullopt;
}

std::vector<int64_t> PhotoRepository::queryByFolder(int64_t folderId, bool pickedOnly) {
    std::vector<int64_t> ids;
    Stmt s = pickedOnly
        ? db_.prepare("SELECT id FROM photos WHERE folder_id=? AND picked=1 ORDER BY COALESCE(capture_time,import_time)")
        : db_.prepare("SELECT id FROM photos WHERE folder_id=? ORDER BY COALESCE(capture_time,import_time)");
    s.bind(1, folderId);
    while (s.step()) ids.push_back(s.getInt64(0));
    return ids;
}

std::vector<int64_t> PhotoRepository::queryAll(bool pickedOnly) {
    std::vector<int64_t> ids;
    Stmt s = pickedOnly
        ? db_.prepare("SELECT id FROM photos WHERE picked=1 ORDER BY COALESCE(capture_time,import_time)")
        : db_.prepare("SELECT id FROM photos ORDER BY COALESCE(capture_time,import_time)");
    while (s.step()) ids.push_back(s.getInt64(0));
    return ids;
}

void PhotoRepository::updatePicked(int64_t id, int picked) {
    auto s = db_.prepare("UPDATE photos SET picked=? WHERE id=?");
    s.bind(1, picked);
    s.bind(2, id);
    s.step();
}

void PhotoRepository::updateThumb(int64_t id, const std::string& path, int w, int h, int64_t mtime) {
    auto s = db_.prepare(
        "UPDATE photos SET thumb_path=?,thumb_width=?,thumb_height=?,thumb_mtime=? WHERE id=?");
    s.bind(1, path);
    s.bind(2, w);
    s.bind(3, h);
    s.bind(4, mtime);
    s.bind(5, id);
    s.step();
}

// ── App settings ──────────────────────────────────────────────────────────────
std::string PhotoRepository::getSetting(const std::string& key, const std::string& def) {
    auto s = db_.prepare("SELECT value FROM app_settings WHERE key=?");
    s.bind(1, key);
    if (s.step()) return s.getText(0);
    return def;
}

void PhotoRepository::setSetting(const std::string& key, const std::string& value) {
    auto s = db_.prepare(
        "INSERT INTO app_settings(key,value) VALUES(?,?)"
        " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    s.bind(1, key);
    s.bind(2, value);
    s.step();
}

} // namespace catalog
