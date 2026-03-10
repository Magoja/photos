#pragma once
#include "Database.h"
#include <string>

namespace catalog {

class Schema {
public:
    // Apply DDL + migrations to the open database.
    // Safe to call on every launch — idempotent.
    // libraryRoot is used for v2 migration (absolute→relative folder paths).
    static void apply(Database& db, const std::string& libraryRoot = "");

    // Current target schema version
    static constexpr int kTargetVersion = 2;
};

} // namespace catalog
