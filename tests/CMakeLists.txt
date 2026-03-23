find_package(Catch2 3 REQUIRED HINTS /opt/homebrew)

set(HB "/opt/homebrew")

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${HB}/include
)

# Shared sources reused by multiple test targets
set(CATALOG_SOURCES
    ${CMAKE_SOURCE_DIR}/src/catalog/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/Schema.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/PhotoRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/ThumbnailCache.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/BackupManager.cpp
)

set(IMPORT_SOURCES_PLAIN
    ${CMAKE_SOURCE_DIR}/src/import/HashDedup.cpp
    ${CMAKE_SOURCE_DIR}/src/import/FileScanner.cpp
    ${CMAKE_SOURCE_DIR}/src/import/RawDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/import/Importer.cpp
)

set(UTIL_SOURCES_PLAIN
    ${CMAKE_SOURCE_DIR}/src/util/ThreadPool.cpp
)

set(COMMON_LIBS
    Catch2::Catch2WithMain
    SQLite::SQLite3
    ${HB}/lib/libspdlog.dylib
    ${HB}/lib/libfmt.dylib
    ${HB}/lib/libturbojpeg.dylib
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
)

set(COMMON_DEFS SPDLOG_COMPILED_LIB)

# ── test_database ─────────────────────────────────────────────────────────────
add_executable(test_database test_database.cpp ${CATALOG_SOURCES})
target_link_libraries(test_database PRIVATE
    Catch2::Catch2WithMain SQLite::SQLite3
    ${HB}/lib/libspdlog.dylib ${HB}/lib/libfmt.dylib
    ${HB}/lib/libturbojpeg.dylib
)
target_compile_definitions(test_database PRIVATE SPDLOG_COMPILED_LIB)
add_test(NAME test_database COMMAND test_database)

# ── test_hash_dedup ───────────────────────────────────────────────────────────
add_executable(test_hash_dedup
    test_hash_dedup.cpp
    ${CATALOG_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/import/HashDedup.cpp
)
target_link_libraries(test_hash_dedup PRIVATE
    Catch2::Catch2WithMain SQLite::SQLite3
    ${HB}/lib/libspdlog.dylib ${HB}/lib/libfmt.dylib
    ${HB}/lib/libturbojpeg.dylib
    ${HB}/lib/libxxhash.dylib
)
target_compile_definitions(test_hash_dedup PRIVATE SPDLOG_COMPILED_LIB)
add_test(NAME test_hash_dedup COMMAND test_hash_dedup)

# ── test_thumb_crop ────────────────────────────────────────────────────────────
add_executable(test_thumb_crop
    test_thumb_crop.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ThumbCropUV.cpp
)
target_link_libraries(test_thumb_crop PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_thumb_crop COMMAND test_thumb_crop)

# ── test_pixel_pipeline ───────────────────────────────────────────────────────
add_executable(test_pixel_pipeline
    test_pixel_pipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/util/PixelPipeline.cpp
)
target_link_libraries(test_pixel_pipeline PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_pixel_pipeline COMMAND test_pixel_pipeline)

# ── test_raw_decoder ──────────────────────────────────────────────────────────
add_executable(test_raw_decoder
    test_raw_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/import/RawDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/util/PixelPipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/ThumbnailCache.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/PhotoRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/Schema.cpp
    ${CMAKE_SOURCE_DIR}/src/catalog/BackupManager.cpp
)
target_compile_definitions(test_raw_decoder PRIVATE
    SPDLOG_COMPILED_LIB
    "TEST_DATA_DIR=\"/Users/magoja/Documents/project/photos test data\""
)
target_link_libraries(test_raw_decoder PRIVATE
    Catch2::Catch2WithMain
    SQLite::SQLite3
    ${HB}/lib/libspdlog.dylib
    ${HB}/lib/libfmt.dylib
    ${HB}/lib/libturbojpeg.dylib
    ${HB}/lib/libraw.dylib
)
add_test(NAME test_raw_decoder COMMAND test_raw_decoder)
