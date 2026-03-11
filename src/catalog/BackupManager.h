#pragma once
#include "Database.h"
#include "PhotoRepository.h"
#include <string>

namespace catalog {

class BackupManager {
 public:
  static constexpr int kMaxBackups = 5;
  static constexpr int kBackupIntervalDays = 7;

  BackupManager(Database& db, const std::string& dbPath, const std::string& backupDir);

  // Check if backup is needed and perform it.
  // Returns true if a backup was created.
  bool checkAndBackup();

  // Rotate: delete oldest backups keeping only kMaxBackups
  void rotate();

 private:
  Database& db_;
  std::string dbPath_;
  std::string backupDir_;

  bool isBackupDue() const;
  std::string doBackup();
  void recordBackup(const std::string& path, int64_t sizeBytes);
};

}  // namespace catalog
