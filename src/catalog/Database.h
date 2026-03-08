#pragma once
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <mutex>
#include <unordered_map>
#include <stdexcept>

namespace catalog {

// ── Exception ────────────────────────────────────────────────────────────────
class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& msg) : std::runtime_error(msg) {}
    DbError(const std::string& msg, int rc)
        : std::runtime_error(msg + " (SQLite rc=" + std::to_string(rc) + ")") {}
};

// ── RAII prepared statement wrapper ──────────────────────────────────────────
class Stmt {
public:
    explicit Stmt(sqlite3_stmt* s) : stmt_(s) {}
    ~Stmt() = default;

    sqlite3_stmt* raw() const { return stmt_; }

    void reset() { sqlite3_reset(stmt_); sqlite3_clear_bindings(stmt_); }

    // Bind helpers (1-indexed)
    void bind(int i, int64_t v)      { sqlite3_bind_int64(stmt_, i, v); }
    void bind(int i, int v)          { sqlite3_bind_int(stmt_, i, v); }
    void bind(int i, double v)       { sqlite3_bind_double(stmt_, i, v); }
    void bind(int i, std::string_view v) {
        sqlite3_bind_text(stmt_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    void bindNull(int i)             { sqlite3_bind_null(stmt_, i); }

    // Returns true when a row is available
    bool step();

    // Column readers (0-indexed)
    int64_t     getInt64(int c)  const { return sqlite3_column_int64(stmt_, c); }
    int         getInt(int c)    const { return sqlite3_column_int(stmt_, c); }
    double      getDouble(int c) const { return sqlite3_column_double(stmt_, c); }
    std::string getText(int c)   const {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, c));
        return p ? p : "";
    }
    bool isNull(int c) const { return sqlite3_column_type(stmt_, c) == SQLITE_NULL; }

private:
    sqlite3_stmt* stmt_;
};

// ── RAII transaction ──────────────────────────────────────────────────────────
class Transaction {
public:
    explicit Transaction(sqlite3* db);
    ~Transaction();
    void commit();
    void rollback();
private:
    sqlite3* db_;
    bool done_ = false;
};

// ── Database ──────────────────────────────────────────────────────────────────
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    sqlite3*    handle() const  { return db_; }
    std::mutex& mutex()         { return mutex_; }

    // Execute arbitrary SQL (no results)
    void exec(std::string_view sql);

    // Cached prepared statement (owned by Database)
    Stmt prepare(const std::string& sql);

    Transaction transaction();

    int64_t lastInsertRowid() const { return sqlite3_last_insert_rowid(db_); }

    // Run a single-column int64 query
    int64_t queryInt64(const std::string& sql, int64_t defaultVal = 0);

private:
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    std::unordered_map<std::string, sqlite3_stmt*> stmtCache_;

    void applyPragmas();
    static void check(int rc, const char* ctx);
};

} // namespace catalog
