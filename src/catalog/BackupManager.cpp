#include "BackupManager.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <ctime>
#include <cstdio>
#include <cstring>

namespace fs = std::filesystem;

namespace catalog {

BackupManager::BackupManager(Database& db,
                              const std::string& dbPath,
                              const std::string& backupDir)
    : db_(db), dbPath_(dbPath), backupDir_(backupDir)
{
    fs::create_directories(backupDir_);
}

bool BackupManager::isBackupDue() const {
    PhotoRepository repo(const_cast<Database&>(db_));
    std::string lastStr = repo.getSetting("last_backup_time", "");
    if (lastStr.empty()) return true;

    // Parse ISO 8601 "YYYY-MM-DDTHH:MM:SS"
    struct tm t {};
    int y,mo,d,h,mi,s;
    if (std::sscanf(lastStr.c_str(), "%d-%d-%dT%d:%d:%d",
                    &y,&mo,&d,&h,&mi,&s) < 3) return true;
    t.tm_year = y - 1900;
    t.tm_mon  = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    t.tm_isdst = -1;
    time_t last = std::mktime(&t);
    time_t now  = std::time(nullptr);
    double days = std::difftime(now, last) / 86400.0;
    return days >= kBackupIntervalDays;
}

std::string BackupManager::doBackup() {
    // Timestamp for filename
    time_t now = std::time(nullptr);
    struct tm* t = std::gmtime(&now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", t);

    std::string destPath = backupDir_ + "/catalog_" + stamp + ".db";

    // Use SQLite backup API for safe hot backup
    sqlite3* dest = nullptr;
    int rc = sqlite3_open(destPath.c_str(), &dest);
    if (rc != SQLITE_OK) {
        spdlog::error("BackupManager: cannot open dest {}", destPath);
        return "";
    }

    sqlite3_backup* bk = sqlite3_backup_init(dest, "main", db_.handle(), "main");
    if (!bk) {
        spdlog::error("BackupManager: sqlite3_backup_init failed");
        sqlite3_close(dest);
        return "";
    }

    sqlite3_backup_step(bk, -1);  // copy all pages
    sqlite3_backup_finish(bk);
    sqlite3_close(dest);

    spdlog::info("BackupManager: backup created at {}", destPath);
    return destPath;
}

void BackupManager::recordBackup(const std::string& path, int64_t sizeBytes) {
    {
        std::lock_guard lk(db_.mutex());
        auto s = db_.prepare(
            "INSERT INTO backup_log(backup_path,size_bytes) VALUES(?,?)");
        s.bind(1, path);
        s.bind(2, sizeBytes);
        s.step();
    }

    // Update last_backup_time
    char stamp[32];
    time_t now = std::time(nullptr);
    struct tm* t = std::gmtime(&now);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", t);

    PhotoRepository repo(db_);
    repo.setSetting("last_backup_time", stamp);
}

void BackupManager::rotate() {
    // Get all backup entries sorted oldest first
    auto s = db_.prepare(
        "SELECT id, backup_path FROM backup_log ORDER BY created_at ASC");

    struct Entry { int64_t id; std::string path; };
    std::vector<Entry> entries;
    while (s.step())
        entries.push_back({ s.getInt64(0), s.getText(1) });

    // Delete oldest beyond kMaxBackups
    int excess = (int)entries.size() - kMaxBackups;
    for (int i = 0; i < excess; ++i) {
        // Delete file
        std::error_code ec;
        fs::remove(entries[i].path, ec);

        // Delete DB row
        std::lock_guard lk(db_.mutex());
        auto d = db_.prepare("DELETE FROM backup_log WHERE id=?");
        d.bind(1, entries[i].id);
        d.step();

        spdlog::debug("BackupManager: rotated out {}", entries[i].path);
    }
}

bool BackupManager::checkAndBackup() {
    if (!isBackupDue()) return false;

    std::string path = doBackup();
    if (path.empty()) return false;

    int64_t sz = 0;
    std::error_code ec;
    if (fs::exists(path, ec))
        sz = static_cast<int64_t>(fs::file_size(path, ec));

    recordBackup(path, sz);
    rotate();
    return true;
}

} // namespace catalog
