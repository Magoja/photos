#include "Database.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace catalog {

// ── Stmt ─────────────────────────────────────────────────────────────────────
bool Stmt::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)  return true;
    if (rc == SQLITE_DONE) return false;
    throw DbError("sqlite3_step failed", rc);
}

// ── Transaction ──────────────────────────────────────────────────────────────
Transaction::Transaction(sqlite3* db) : db_(db) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw DbError("BEGIN failed: " + msg);
    }
}

Transaction::~Transaction() {
    if (!done_) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

void Transaction::commit() {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw DbError("COMMIT failed: " + msg);
    }
    done_ = true;
}

void Transaction::rollback() {
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    done_ = true;
}

// ── Database ──────────────────────────────────────────────────────────────────
Database::Database(const std::string& path) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    check(sqlite3_open_v2(path.c_str(), &db_, flags, nullptr), "open");
    applyPragmas();
    spdlog::debug("Database opened: {}", path);
}

Database::~Database() {
    for (auto& [sql, stmt] : stmtCache_)
        sqlite3_finalize(stmt);
    stmtCache_.clear();
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void Database::applyPragmas() {
    exec("PRAGMA journal_mode = WAL");
    exec("PRAGMA synchronous  = NORMAL");
    exec("PRAGMA cache_size   = -32000");
    exec("PRAGMA mmap_size    = 536870912");
    exec("PRAGMA foreign_keys = ON");
}

void Database::exec(std::string_view sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw DbError("exec failed: " + msg, rc);
    }
}

Stmt Database::prepare(const std::string& sql) {
    auto it = stmtCache_.find(sql);
    if (it != stmtCache_.end()) {
        sqlite3_reset(it->second);
        sqlite3_clear_bindings(it->second);
        return Stmt(it->second);
    }
    sqlite3_stmt* s = nullptr;
    check(sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr), "prepare");
    stmtCache_[sql] = s;
    return Stmt(s);
}

Transaction Database::transaction() {
    return Transaction(db_);
}

int64_t Database::queryInt64(const std::string& sql, int64_t defaultVal) {
    auto stmt = prepare(sql);
    if (stmt.step()) return stmt.getInt64(0);
    return defaultVal;
}

void Database::check(int rc, const char* ctx) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw DbError(std::string(ctx) + " failed", rc);
}

} // namespace catalog
