#include "Schema.h"
#include <spdlog/spdlog.h>

namespace catalog {

static constexpr const char* kDDL = R"SQL(

CREATE TABLE IF NOT EXISTS schema_version (
    version    INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS volumes (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid       TEXT    NOT NULL UNIQUE,
    label      TEXT    NOT NULL DEFAULT '',
    mount_path TEXT    NOT NULL DEFAULT '',
    last_seen  TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS folders (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id  INTEGER REFERENCES folders(id) ON DELETE CASCADE,
    volume_id  INTEGER REFERENCES volumes(id) ON DELETE CASCADE,
    path       TEXT    NOT NULL UNIQUE,
    name       TEXT    NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS photos (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    folder_id       INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE,
    filename        TEXT    NOT NULL,
    file_hash       TEXT,
    file_size       INTEGER NOT NULL DEFAULT 0,
    import_time     TEXT    NOT NULL DEFAULT (datetime('now')),
    capture_time    TEXT,
    camera_make     TEXT,
    camera_model    TEXT,
    lens_model      TEXT,
    focal_length_mm REAL,
    aperture        REAL,
    shutter_speed   TEXT,
    iso             INTEGER,
    width_px        INTEGER,
    height_px       INTEGER,
    gps_lat         REAL,
    gps_lon         REAL,
    gps_alt_m       REAL,
    picked          INTEGER NOT NULL DEFAULT 0,
    rating          INTEGER NOT NULL DEFAULT 0,
    color_label     TEXT,
    thumb_path      TEXT,
    thumb_width     INTEGER,
    thumb_height    INTEGER,
    thumb_mtime     INTEGER,
    edit_settings   TEXT    NOT NULL DEFAULT '{}',
    UNIQUE(folder_id, filename)
);

CREATE TABLE IF NOT EXISTS tags (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT    NOT NULL UNIQUE COLLATE NOCASE
);

CREATE TABLE IF NOT EXISTS photo_tags (
    photo_id INTEGER NOT NULL REFERENCES photos(id) ON DELETE CASCADE,
    tag_id   INTEGER NOT NULL REFERENCES tags(id)   ON DELETE CASCADE,
    PRIMARY KEY (photo_id, tag_id)
);

CREATE TABLE IF NOT EXISTS export_presets (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL UNIQUE,
    quality     INTEGER NOT NULL DEFAULT 85,
    max_width   INTEGER NOT NULL DEFAULT 0,
    max_height  INTEGER NOT NULL DEFAULT 0,
    target_path TEXT    NOT NULL DEFAULT '',
    config_json TEXT    NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS app_settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS backup_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    backup_path TEXT    NOT NULL,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    size_bytes  INTEGER NOT NULL DEFAULT 0
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_photos_folder   ON photos(folder_id);
CREATE INDEX IF NOT EXISTS idx_photos_hash     ON photos(file_hash);
CREATE INDEX IF NOT EXISTS idx_photos_capture  ON photos(capture_time);
CREATE INDEX IF NOT EXISTS idx_photos_picked   ON photos(picked);
CREATE INDEX IF NOT EXISTS idx_photos_import   ON photos(import_time);
CREATE INDEX IF NOT EXISTS idx_folders_parent  ON folders(parent_id);
CREATE INDEX IF NOT EXISTS idx_folders_volume  ON folders(volume_id);
CREATE INDEX IF NOT EXISTS idx_photo_tags_tag  ON photo_tags(tag_id);

)SQL";

static constexpr const char* kSeedPresets = R"SQL(
INSERT OR IGNORE INTO export_presets (name, quality, max_width, max_height)
    VALUES ('High Quality',  92, 0,    0);
INSERT OR IGNORE INTO export_presets (name, quality, max_width, max_height)
    VALUES ('Medium Quality', 75, 2048, 2048);
INSERT OR IGNORE INTO export_presets (name, quality, max_width, max_height)
    VALUES ('Low / Web',      60, 1024, 1024);
)SQL";

// ── Migration helpers ─────────────────────────────────────────────────────────

static void applyV1(Database& db) {
  db.exec(kSeedPresets);
  db.exec("INSERT OR IGNORE INTO schema_version(version) VALUES (1)");
  spdlog::info("Schema v1 applied");
}

static void migrateToRelativePaths(Database& db, const std::string& libraryRoot) {
  // Strip the library root prefix from folder paths, converting absolute
  // paths to portable relative paths (SUBSTR is 1-based, +1 for separator).
  size_t offset = libraryRoot.size() + 2;
  db.exec("UPDATE folders SET path = SUBSTR(path, " + std::to_string(offset) +
          ") WHERE path LIKE '" + libraryRoot + "/%'");
  db.exec("INSERT OR IGNORE INTO schema_version(version) VALUES (2)");
  spdlog::info("Schema v2 applied: migrated folder paths to relative");
}

// ── Schema::apply ─────────────────────────────────────────────────────────────

void Schema::apply(Database& db, const std::string& libraryRoot) {
  db.exec(kDDL);

  int64_t ver = db.queryInt64("SELECT MAX(version) FROM schema_version", 0);

  if (ver < 1)
    applyV1(db);
  if (ver < 2 && !libraryRoot.empty())
    migrateToRelativePaths(db, libraryRoot);

  spdlog::debug("Schema version: {}", ver < 1 ? 1 : (int)ver);
}

}  // namespace catalog
