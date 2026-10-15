# Tests link against photos_core, so they build on any platform where the
# core builds — no Homebrew paths, no .dylib names, no UI sources.

find_package(Catch2 3 REQUIRED)

# ── test_database ────────────────────────────────────────────────────────────
add_executable(test_database test_database.cpp)
target_link_libraries(test_database PRIVATE photos_core Catch2::Catch2WithMain)
add_test(NAME test_database COMMAND test_database)

# ── test_hash_dedup ──────────────────────────────────────────────────────────
add_executable(test_hash_dedup test_hash_dedup.cpp)
target_link_libraries(test_hash_dedup PRIVATE photos_core Catch2::Catch2WithMain)
add_test(NAME test_hash_dedup COMMAND test_hash_dedup)

# ── test_thumb_crop ──────────────────────────────────────────────────────────
add_executable(test_thumb_crop test_thumb_crop.cpp)
target_link_libraries(test_thumb_crop PRIVATE photos_core Catch2::Catch2WithMain)
add_test(NAME test_thumb_crop COMMAND test_thumb_crop)

# ── test_pixel_pipeline ──────────────────────────────────────────────────────
add_executable(test_pixel_pipeline test_pixel_pipeline.cpp)
target_link_libraries(test_pixel_pipeline PRIVATE photos_core Catch2::Catch2WithMain)
add_test(NAME test_pixel_pipeline COMMAND test_pixel_pipeline)

# ── test_raw_decoder ─────────────────────────────────────────────────────────
# Needs a real RAW file from a machine-local data folder. The folder can be
# pointed elsewhere with -DPHOTOS_TEST_DATA_DIR=...; when the file is absent
# the test binary still builds but is not registered with CTest.
set(PHOTOS_TEST_DATA_DIR "/Users/magoja/Documents/project/photos test data"
    CACHE PATH "Folder holding the RAW test data (20260219/5F1A2661.CR2)")

add_executable(test_raw_decoder test_raw_decoder.cpp)
target_compile_definitions(test_raw_decoder PRIVATE
    "TEST_DATA_DIR=\"${PHOTOS_TEST_DATA_DIR}\""
)
target_link_libraries(test_raw_decoder PRIVATE photos_core Catch2::Catch2WithMain)
if(EXISTS "${PHOTOS_TEST_DATA_DIR}/20260219/5F1A2661.CR2")
  add_test(NAME test_raw_decoder COMMAND test_raw_decoder)
else()
  message(STATUS "test_raw_decoder: RAW test data not found under "
                 "${PHOTOS_TEST_DATA_DIR} — target built but not registered")
endif()

# ── test_exif_parser ─────────────────────────────────────────────────────────
add_executable(test_exif_parser test_exif_parser.cpp)
target_link_libraries(test_exif_parser PRIVATE photos_core Catch2::Catch2WithMain)
add_test(NAME test_exif_parser COMMAND test_exif_parser)

# ── test_command ─────────────────────────────────────────────────────────────
add_executable(test_command
    test_command_registry.cpp
    test_command_image.cpp
    test_command_catalog.cpp
    test_command_metasync.cpp
)
target_link_libraries(test_command PRIVATE photos_core Catch2::Catch2WithMain)
add_test(NAME test_command COMMAND test_command)
