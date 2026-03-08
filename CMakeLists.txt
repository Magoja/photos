cmake_minimum_required(VERSION 3.25)
project(PhotoLibrary VERSION 0.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ── Homebrew prefix ──────────────────────────────────────────────────────────
set(HB "/opt/homebrew")
list(APPEND CMAKE_PREFIX_PATH "${HB}")

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(SDL2   REQUIRED)
find_package(LibRaw REQUIRED)
find_package(SQLite3 REQUIRED)

# xxhash (header-only use + optional lib)
find_path(XXHASH_INCLUDE_DIR NAMES xxhash.h HINTS "${HB}/include" REQUIRED)
find_library(XXHASH_LIBRARY  NAMES xxhash   HINTS "${HB}/lib"     REQUIRED)

# libjpeg-turbo
find_path(JPEG_INCLUDE_DIR   NAMES turbojpeg.h HINTS "${HB}/include" REQUIRED)
find_library(JPEG_LIBRARY    NAMES turbojpeg   HINTS "${HB}/lib"     REQUIRED)

# nlohmann/json
find_path(NLOHMANN_INCLUDE_DIR NAMES nlohmann/json.hpp HINTS "${HB}/include" REQUIRED)

# spdlog + fmt (spdlog compiled lib bundles fmt)
find_path(SPDLOG_INCLUDE_DIR NAMES spdlog/spdlog.h HINTS "${HB}/include" REQUIRED)
find_library(SPDLOG_LIBRARY  NAMES spdlog           HINTS "${HB}/lib"     REQUIRED)
find_library(FMT_LIBRARY     NAMES fmt              HINTS "${HB}/lib"     REQUIRED)

# ── ImGui sources ─────────────────────────────────────────────────────────────
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_metal.mm
)

# ── Application sources ───────────────────────────────────────────────────────
set(APP_SOURCES
    src/main.mm
    ${IMGUI_SOURCES}
)

add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${APP_SOURCES})

# ── Include dirs ──────────────────────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
    ${XXHASH_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${LIBRAW_INCLUDE_DIR}
    ${SQLite3_INCLUDE_DIRS}
)

# ── Link ─────────────────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARIES}
    LibRaw::LibRaw
    SQLite::SQLite3
    ${XXHASH_LIBRARY}
    ${JPEG_LIBRARY}
    ${SPDLOG_LIBRARY}
    ${FMT_LIBRARY}
    "-framework Metal"
    "-framework MetalKit"
    "-framework Cocoa"
    "-framework QuartzCore"
    "-framework DiskArbitration"
    "-framework IOKit"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SPDLOG_COMPILED_LIB
    $<$<CONFIG:Debug>:DEBUG>
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# ── macOS bundle settings ─────────────────────────────────────────────────────
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.photolib.app"
    MACOSX_BUNDLE_BUNDLE_NAME    "Photo Library"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    XCODE_ATTRIBUTE_ARCHS        "arm64"
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
