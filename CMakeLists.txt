cmake_minimum_required(VERSION 3.25)

# The app target is macOS-only (Obj-C++, Metal, Cocoa). The photos_core
# library and its tests are portable C++23 and build on any platform.
if(APPLE)
  project(JakeutilPhotos VERSION 0.1.0 LANGUAGES CXX OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
else()
  project(JakeutilPhotos VERSION 0.1.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(PHOTOS_BUILD_APP "Build the macOS app" ${APPLE})
option(PHOTOS_BUILD_TESTS "Build the unit tests" ON)

if(PHOTOS_BUILD_APP AND NOT APPLE)
  message(FATAL_ERROR "PHOTOS_BUILD_APP requires macOS (Metal/Cocoa). "
                      "Set PHOTOS_BUILD_APP=OFF to build photos_core + tests only.")
endif()

# ── Homebrew prefix (macOS only; elsewhere system paths are searched) ───────
set(HB "")
if(APPLE AND EXISTS "/opt/homebrew")
  set(HB "/opt/homebrew")
  list(APPEND CMAKE_PREFIX_PATH "${HB}")
endif()

# ── Core dependencies ───────────────────────────────────────────────────────
find_package(SQLite3 REQUIRED)
find_package(LibRaw REQUIRED)
find_package(Threads REQUIRED)

# xxhash (header + lib)
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

# ── photos_core: portable, non-UI sources ────────────────────────────────────
# Everything here must stay free of ImGui/SDL/Metal/Cocoa includes so the
# tests can build and run without a Mac. Platform dialogs are injected by
# the app (see ExportSession::setConflictCallback).
set(CATALOG_SOURCES
    src/catalog/Database.cpp
    src/catalog/Schema.cpp
    src/catalog/PhotoRepository.cpp
    src/catalog/ThumbnailCache.cpp
    src/catalog/BackupManager.cpp
)

set(IMPORT_CORE_SOURCES
    src/import/FileScanner.cpp
    src/import/ImageDecoder.cpp
    src/import/RawDecoder.cpp
    src/import/ExifParser.cpp
    src/import/HashDedup.cpp
    src/import/Importer.cpp
)

set(EXPORT_SOURCES
    src/export/Exporter.cpp
    src/export/ExifWriter.cpp
    src/export/ExportSession.cpp
)

set(COMMAND_CORE_SOURCES
    src/command/CommandRegistry.cpp
    src/command/handlers/ImageAdjustHandler.cpp
    src/command/handlers/ImageRevertHandler.cpp
    src/command/handlers/ImageCropHandler.cpp
    src/command/handlers/ImageSaveHandler.cpp
    src/command/handlers/CatalogPickHandler.cpp
    src/command/handlers/CatalogOpenHandler.cpp
    src/command/handlers/CatalogDeletePhotosHandler.cpp
    src/command/handlers/CatalogDeleteFolderHandler.cpp
    src/command/handlers/MetaSyncHandler.cpp
    src/command/handlers/ExportHandler.cpp
    src/command/handlers/DebugReimportMetadataHandler.cpp
)

set(UTIL_CORE_SOURCES
    src/util/ThreadPool.cpp
    src/util/PixelPipeline.cpp
    src/util/ImageLoader.cpp
)

add_library(photos_core STATIC
    ${CATALOG_SOURCES}
    ${IMPORT_CORE_SOURCES}
    ${EXPORT_SOURCES}
    ${COMMAND_CORE_SOURCES}
    ${UTIL_CORE_SOURCES}
    src/ui/ThumbCropUV.cpp
)

target_include_directories(photos_core PUBLIC
    src
    ${XXHASH_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${LIBRAW_INCLUDE_DIR}
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(photos_core PUBLIC
    LibRaw::LibRaw
    SQLite::SQLite3
    ${XXHASH_LIBRARY}
    ${JPEG_LIBRARY}
    ${SPDLOG_LIBRARY}
    ${FMT_LIBRARY}
    Threads::Threads
)

# Consumers include command/CommandRegistry.h, which includes spdlog headers,
# so the compiled-lib define must match the library build everywhere.
target_compile_definitions(photos_core PUBLIC SPDLOG_COMPILED_LIB)

target_compile_options(photos_core PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
)

# ── macOS app ────────────────────────────────────────────────────────────────
if(PHOTOS_BUILD_APP)
  find_package(SDL2 REQUIRED)

  set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")

  # imgui_impl_metal.mm uses __bridge_transfer/__bridge_retained without ARC;
  # suppress the harmless "no effect without ARC" warning for that file only.
  set_source_files_properties(
      ${IMGUI_DIR}/backends/imgui_impl_metal.mm
      PROPERTIES COMPILE_OPTIONS "-Wno-arc-bridge-casts-disallowed-in-nonarc"
  )

  # main.mm and EditView.mm include command/ headers that use std::expected (C++23).
  # Only these two .mm files need C++23; the rest compile fine under C++20.
  set_source_files_properties(
      src/main.mm
      src/ui/EditView.mm
      PROPERTIES COMPILE_OPTIONS "-std=gnu++23"
  )

  set(IMGUI_SOURCES
      ${IMGUI_DIR}/imgui.cpp
      ${IMGUI_DIR}/imgui_draw.cpp
      ${IMGUI_DIR}/imgui_tables.cpp
      ${IMGUI_DIR}/imgui_widgets.cpp
      ${IMGUI_DIR}/imgui_demo.cpp
      ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
      ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
      ${IMGUI_DIR}/backends/imgui_impl_metal.mm
  )

  # App-only sources: UI, platform glue, and startup command wiring
  # (AppCommands registers handlers against live UI objects).
  set(UI_SOURCES
      src/ui/TextureManager.mm
      src/ui/Renderer.mm
      src/ui/GridView.cpp
      src/ui/FolderTreePanel.cpp
      src/ui/FilterBar.cpp
      src/ui/FullscreenView.cpp
      src/ui/EditView.mm
      src/ui/ImportDialog.cpp
      src/ui/ExportDialog.cpp
      src/ui/MetaSyncDialog.cpp
      src/ui/DeleteConfirmDialog.cpp
      src/ui/SettingsPanel.cpp
      src/ui/ThumbEditApplier.cpp
  )

  set(APP_ICON "${CMAKE_SOURCE_DIR}/assets/JakeutilPhotos.icns")
  set_source_files_properties(${APP_ICON} PROPERTIES
      MACOSX_PACKAGE_LOCATION "Resources"
  )

  add_executable(${PROJECT_NAME} MACOSX_BUNDLE
      src/main.mm
      ${IMGUI_SOURCES}
      ${UI_SOURCES}
      src/import/VolumeWatcher.mm
      src/import/PreviewScanner.cpp
      src/command/AppCommands.cpp
      src/util/Platform.mm
      ${APP_ICON}
  )

  target_include_directories(${PROJECT_NAME} PRIVATE
      ${IMGUI_DIR}
      ${IMGUI_DIR}/backends
      ${SDL2_INCLUDE_DIRS}
  )

  target_link_libraries(${PROJECT_NAME} PRIVATE
      photos_core
      ${SDL2_LIBRARIES}
      "-framework Metal"
      "-framework MetalKit"
      "-framework Cocoa"
      "-framework QuartzCore"
      "-framework DiskArbitration"
      "-framework IOKit"
      "-framework UniformTypeIdentifiers"
  )

  target_compile_definitions(${PROJECT_NAME} PRIVATE
      $<$<CONFIG:Debug>:DEBUG>
  )

  target_compile_options(${PROJECT_NAME} PRIVATE
      -Wall
      -Wextra
      -Wno-unused-parameter
      -Wno-missing-field-initializers
      $<$<CONFIG:Release>:-O3 -DNDEBUG>
  )

  set_target_properties(${PROJECT_NAME} PROPERTIES
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.jakeutil.photos"
      MACOSX_BUNDLE_BUNDLE_NAME    "Jakeutil Photos"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_ICON_FILE      "JakeutilPhotos.icns"
      XCODE_ATTRIBUTE_ARCHS        "arm64"
  )
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
if(PHOTOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
