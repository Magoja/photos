find_package(Catch2 3 REQUIRED HINTS /opt/homebrew)

set(HB "/opt/homebrew")

# Shared include paths for all test targets
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${HB}/include
)

# Shared catalog sources used by tests
set(CATALOG_SOURCES
    ${CMAKE_SOURCE_DIR}/src/catalog/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/Schema.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/PhotoRepository.cpp
)

# ── test_database ──────────────────────────────────────────────────────────
add_executable(test_database test_database.cpp ${CATALOG_SOURCES})
target_link_libraries(test_database PRIVATE
    Catch2::Catch2WithMain
    SQLite::SQLite3
    ${HB}/lib/libspdlog.dylib
    ${HB}/lib/libfmt.dylib
)
target_compile_definitions(test_database PRIVATE SPDLOG_COMPILED_LIB)
add_test(NAME test_database COMMAND test_database)

# ── test_hash_dedup (Task 6) ───────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_hash_dedup.cpp")
    add_executable(test_hash_dedup
        test_hash_dedup.cpp
        ${CMAKE_SOURCE_DIR}/src/import/HashDedup.cpp
        ${CATALOG_SOURCES}
    )
    target_link_libraries(test_hash_dedup PRIVATE
        Catch2::Catch2WithMain
        SQLite::SQLite3
        ${HB}/lib/libspdlog.dylib
        ${HB}/lib/libfmt.dylib
        ${HB}/lib/libxxhash.dylib
    )
    target_compile_definitions(test_hash_dedup PRIVATE SPDLOG_COMPILED_LIB)
    add_test(NAME test_hash_dedup COMMAND test_hash_dedup)
endif()
