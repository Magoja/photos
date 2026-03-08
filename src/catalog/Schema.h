#pragma once
#include "Database.h"

namespace catalog {

class Schema {
public:
    // Apply DDL + migrations to the open database.
    // Safe to call on every launch — idempotent.
    static void apply(Database& db);

    // Current target schema version
    static constexpr int kTargetVersion = 1;
};

} // namespace catalog
