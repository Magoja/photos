# Photo Library Manager

## How to continue
Say "Continue the project" to pick up the next unchecked task below.
Claude will: read this file → implement the next `- [ ]` task → run verification → **create a git commit** → check it off `- [x]` → stop.

## Stack
- C++20 + Obj-C++ (.mm) for macOS APIs (Metal, DiskArbitration, NSOpenPanel)
- Dear ImGui (git submodule `third_party/imgui`, docking branch) + SDL2 + Metal backend
- LibRaw 0.21 (RAW decode + embedded JPEG thumb extraction)
- libjpeg-turbo (thumbnail encode/decode, export JPEG)
- SQLite3 raw C API (WAL mode, prepared statement pool)
- xxhash XXH3 (dedup fingerprinting)
- nlohmann/json (app config + `edit_settings` column)
- spdlog (logging to `~/Library/Logs/PhotoLibrary/`)
- CMake 3.25 + CMakePresets.json (`debug` / `release`, arm64)
- Catch2 v3 (unit tests)
- All Homebrew dependencies; ImGui as git submodule only

## Build commands
```bash
cmake --preset debug
cmake --build build/debug
./build/debug/PhotoLibrary          # run app
ctest --preset debug --output-on-failure  # run tests
```

## Key architectural rules
- DB writes: background/import thread only, guarded by std::mutex on Database instance
- Thumbnail upload: background ThreadPool decodes JPEG → posts ThumbnailResult to main-thread queue → main thread uploads MTLTexture
- Virtual scroll: preload photo IDs into std::vector<int64_t> for current folder; render only rows in viewport using SetCursorPosY skip
- Dedup: XXH3-64 fast-fingerprint (first+last 64 KB + size) in memory per session; full XXH3-128 only on fingerprint collision; full hash stored in DB
- edit_settings: TEXT column, JSON blob via nlohmann/json — extensibility hook for future non-destructive edits
- Metal textures: MTLStorageModeShared on Apple Silicon (unified memory, no blit needed)
- LRU texture cache: 2000 slot cap; placeholder gray texture returned while async load is in flight

## Dependency graph
```
1 (Scaffold)
├── 2 (Database) ──── 5 (Thumbs) ─┐
│                └── 13 (Backup)  │
├── 3 (USB)      ──────────────── 7 (Import) ── 10 (Import UI)
├── 4 (RAW)      ──── 5 (Thumbs) ─┘
├── 6 (Dedup)    ──────────────── 7 (Import)
└── 8 (Renderer) ──── 9 (Grid)  ─── (needs 2,5)
                 ├─── 10 (Import UI)
                 ├─── 11 (Fullscreen) ─── (needs 2)
                 └─── 12 (Export)     ─── (needs 2)
```

## Tasks

- [x] **Task 1 — Project scaffold & build system**
  Files: `CMakeLists.txt`, `CMakePresets.json`, `cmake/CompilerFlags.cmake`, `src/main.mm`, `.gitmodules`, `.gitignore`
  ✓ Verify: `cmake --preset debug && cmake --build build/debug` succeeds with 0 errors/warnings. Running the binary opens a window titled "Photo Library" that closes cleanly on Cmd+Q.

- [x] **Task 2 — Catalog database layer**
  Files: `src/catalog/Database.h/.cpp`, `Schema.h/.cpp`, `PhotoRepository.h/.cpp`, `tests/test_database.cpp`
  ✓ Verify: `ctest --preset debug` passes all DB tests. `EXPLAIN QUERY PLAN` for folder query shows `USING INDEX idx_photos_folder`. Insert of duplicate (folder_id, filename) throws/returns error.

- [ ] **Task 3 — Volume/USB detection**
  Files: `src/import/VolumeWatcher.h/.mm`, `src/util/Platform.h/.mm`
  ✓ Verify: Plug in USB → spdlog console shows `Volume mounted uuid=X path=/Volumes/...` within 500 ms. Unplug → `Volume unmounted`. `volumes` table row created/updated.

- [ ] **Task 4 — RAW decoding & EXIF extraction**
  Files: `src/import/RawDecoder.h/.cpp`, `ExifParser.h/.cpp`
  ✓ Verify: Unit test with a sample `.CR3` or `.ARW` file passes: thumbnail JPEG bytes non-empty and valid (libjpeg-turbo can decode them), `ExifData.cameraMake` non-empty, `captureTime` is valid ISO 8601.

- [ ] **Task 5 — Thumbnail cache**
  Files: `src/catalog/ThumbnailCache.h/.cpp`
  ✓ Verify: Generate thumbnails for 50 RAW files. Files appear at `~/Library/Caches/PhotoLibrary/thumbs/{xx}/{hash}.jpg`. Each is ≤ 256×256 px and a valid JPEG. `photos.thumb_path` updated in DB.

- [ ] **Task 6 — Hash dedup**
  Files: `src/import/HashDedup.h/.cpp`, `tests/test_hash_dedup.cpp`
  ✓ Verify: Tests pass: identical file → same hash on two calls; 1-byte changed file → different hash; `isDuplicate(db, hash)` returns photo_id for a hash already in DB, nullopt otherwise.

- [ ] **Task 7 — Import pipeline**
  Files: `src/import/Importer.h/.cpp`, `FileScanner.h/.cpp`, `src/util/ThreadPool.h/.cpp`
  ✓ Verify: Import 200 mixed RAW+JPEG files. Duplicates (injected by copying one file twice) are skipped with log message. Target has `YYYY-MM-DD` subfolders from EXIF date. Progress callback fires per file. DB has all non-duplicate entries.

- [ ] **Task 8 — Metal renderer & TextureManager**
  Files: `src/ui/Renderer.h/.mm`, `TextureManager.h/.cpp`
  ✓ Verify: 2000 placeholder textures render in a grid at ≥ 55 fps (use ImGui frame-time overlay). LRU evicts correctly at 2001st request. Textures created with `MTLStorageModeShared`.

- [ ] **Task 9 — Grid view UI**
  Files: `src/ui/GridView.h/.cpp`, `FolderTreePanel.h/.cpp`, `FilterBar.h/.cpp`
  ✓ Verify: 10,000-photo catalog scrolls at ≥ 55 fps. Folder tree shows correct per-folder photo counts. Switching All↔Picked filter updates grid in one frame. Unavailable-volume photos show `?` badge.

- [ ] **Task 10 — Import dialog UI**
  Files: `src/ui/ImportDialog.h/.cpp`
  ✓ Verify: USB drive with 50 RAW files → preview grid loads all 50 thumbs within 10 s. Import → progress bar advances → dialog reports "50 new, 0 duplicates". Cancel button halts mid-import.

- [ ] **Task 11 — Fullscreen view & pick toggle**
  Files: `src/ui/FullscreenView.h/.cpp`
  ✓ Verify: `F` opens fullscreen; full-res RAW renders within 3 s (45 MP file). Left/Right arrows navigate. Backtick toggles picked; switching back to Grid with Picked filter shows the toggled photo. Scroll-wheel zooms, drag pans.

- [ ] **Task 12 — Export pipeline**
  Files: `src/export/Exporter.h/.cpp`, `ExportPreset.h`, `src/ui/ExportDialog.h/.cpp`
  ✓ Verify: Select 20 RAW photos → "Medium Quality" export → 20 JPEG files on Desktop, each ≤ 2048px longest edge, JPEG quality ~75. Completes in < 60 s on M-series. Preset's `target_path` saved to DB.

- [ ] **Task 13 — Weekly catalog backup**
  Files: `src/catalog/BackupManager.h/.cpp`
  ✓ Verify: Set `app_settings.last_backup_time` = 8 days ago → launch app → `.db` backup appears in `~/Library/Application Support/PhotoLibrary/backups/` within 30 s. With 6 artificial backup rows, cleanup leaves exactly 5 files and 5 `backup_log` rows.
