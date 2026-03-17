# Photo Library Manager

## How to continue
Say "Continue the project" to pick up the next unchecked task below.
Claude will: read this file → implement the next `- [ ]` task → run verification → **create a git commit** → check it off `- [x]` → stop.

## Bug / crash fixes
When fixing a crash or bug: implement the fix, build, and explain the change — but **do NOT commit** until the user confirms the fix works.

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
- Thumbnail pyramid: two tiers — micro (64px, `thumbs_micro/{xx}/{hash}.jpg`) loads first as blurry placeholder; standard (256px, `thumbs/{xx}/{hash}.jpg`) replaces it. Micro textures use `photoId + kMicroOffset` (1 000 000 000) as composite LRU key.
- Virtual scroll: preload photo IDs into std::vector<int64_t> for current folder; render only rows in viewport using SetCursorPosY skip
- Dedup: XXH3-64 fast-fingerprint (first+last 64 KB + size) in memory per session; full XXH3-128 only on fingerprint collision; full hash stored in DB
- edit_settings: TEXT column, JSON blob via nlohmann/json — extensibility hook for future non-destructive edits
- Metal textures: MTLStorageModeShared on Apple Silicon (unified memory, no blit needed)
- LRU texture cache: 2000 slot cap; placeholder gray texture returned while async load is in flight
- EditView::render() is a 5-line composition: `handleKeyCapture` → `renderPreviewArea` → `renderControlPanel` (→ `renderModeTabs`, `renderSaveButtons`) → `pollSaveCompletion`

## Code style
- **Single responsibility**: every function does exactly one thing.
  Extract a block into a named helper if ANY of these apply:
  (a) it needs an explanatory comment to understand;
  (b) it performs a distinguishable named action (e.g. "toggle pick", "copy file", "parse timestamp");
  (c) it could be tested independently.
  Prefer private methods for class member access, free functions otherwise.
- **`const` correctness**: mark every variable, parameter, and method `const` unless it must be
  mutated. Use `const auto` for local variables by default; only drop `const` when assignment or
  mutation is required. Mark member functions `const` when they do not modify object state.
- **C++20 features**: prefer `std::ranges` / `std::views` algorithms over raw loops; use structured bindings, `std::span`, `std::format` (where available), and concepts/requires-clauses where they improve clarity. Avoid hand-rolled loops when a standard algorithm expresses intent more directly.
- **Indentation**: 2 spaces (no tabs). Enforced by `.clang-format` at project root.
- **Braces**: always use curly braces for `if`/`else`/`for`/`while` bodies, even single-line. Enforced by `.clang-format`.
- **Data-conversion helpers**: any inline block that converts data from one representation to another (e.g. RGB→RGBA, pixels→MTLTexture, JSON→struct) must be extracted into a named free function. Name it `aToB` or `aFromB`. Place it in the nearest anonymous namespace. Never leave a multi-line conversion loop or construction sequence inline inside a larger function.

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

9 (Grid) ──── 16 (Multi-select) ─┬─ 17 (Meta Sync)
                                  └─ 18 (Export v2)
```

## Tasks

- [x] **Task 16 — Multi-selection in Grid**
  Files: `src/ui/GridView.h/.cpp`, `src/main.mm`

  - Replace `int64_t selectedId_` with `int64_t primaryId_` + `std::unordered_set<int64_t> selectedIds_`
  - Click (no modifier): clear selection, set new primary → same behavior as today for single-photo open/edit
  - Cmd+click: toggle photo in `selectedIds_`; if ≥1 selected, first selection becomes primary
  - Shift+click: range-select from `primaryId_` to clicked photo in the grid order
  - Visual: gold border for `primaryId_`, blue border for other selected, gray for unselected
  - Show "N selected" label in grid header when `selectedIds_` non-empty
  - Add `using MultiSelectCb = std::function<void(std::vector<int64_t>)>` callback
  - `main.mm`: when selection ≥ 2, show "Sync Metadata" and "Export" toolbar buttons that open respective dialogs
  - `selectedIds_` is in-memory only (no DB persistence), reset on folder change
  - `primaryId_` replaces old `selectedId_` for single-photo open/fullscreen behavior

  ✓ Verify: Cmd+click 5 photos → 5 blue borders + "5 selected" label. Shift+click selects contiguous range. Plain click clears multi-selection and selects single. "Sync Metadata" and "Export" buttons appear in toolbar when ≥2 selected.

- [x] **Task 17 — Metadata Sync Dialog**
  Files: `src/ui/MetaSyncDialog.h/.cpp`, `src/catalog/PhotoRepository.h/.cpp`, `src/main.mm`

  *PhotoRepository additions:*
  - `updateEditSettingsBulk(std::vector<int64_t> ids, std::string json)` — wraps individual `updateEditSettings` calls in a single `Transaction`

  *MetaSyncDialog:*
  - Constructor takes `PhotoRepository&` and `ThumbnailCache&`
  - `open(int64_t primaryId, std::vector<int64_t> targetIds)` — loads source photo's `EditSettings` from DB
  - `render()` — modal with:
    - Source photo thumbnail + filename header
    - Checkbox "Adjustments" (Adjust tab fields: exposure, temperature, contrast, saturation) — `static bool` persists between opens
    - Checkbox "Crop" (Crop tab fields: x, y, w, h, angleDeg) — `static bool` persists between opens
    - "Sync N photos" button → merges checked fields into each target's existing `EditSettings` JSON → calls `updateEditSettingsBulk` → fires `DoneCb`
  - Merge logic: load target's current `EditSettings`, overwrite only the checked sub-fields, write back — so unchecked fields are untouched
  - After sync: enqueue thumbnail regeneration for each modified photo via `ThumbnailCache::generateAsync`

  *main.mm:*
  - Wire "Sync Metadata" button → `metaSyncDialog_.open(primaryId, selectedIds)`
  - On `DoneCb`: call `gridView_.reload()` to refresh thumbnails

  ✓ Verify: Select 5 photos where photo 1 has exposure=1.5. Open Sync → check "Adjustments" only → Sync → all 5 photos have exposure=1.5 in DB (`SELECT edit_settings FROM photos WHERE id IN (...)`). Crop fields unchanged. Thumbnails regenerate in grid.

- [x] **Task 18 — Export Selected as Google Photos JPEG**
  Files: `src/export/Exporter.h/.cpp`, `src/ui/ExportDialog.h/.cpp`, `src/main.mm`

  *ExportDialog:*
  - `open(int64_t primaryId, std::vector<int64_t> ids)` — replaces current single-photo open signature
  - Remove preset selector; use single hardcoded "Google Photos" config: quality=90, maxDim=0 (full-res)
  - Show target folder picker (NSOpenPanel, directory only) + "Export N photos" button
  - Blocking modal: stays open during export showing progress bar + "done / total" file counter + Cancel button; dismisses automatically when complete

  *Exporter:*
  - Per photo: use `LibRaw::unpack()` + `LibRaw::dcraw_process()` for full-res decode (instead of embedded JPEG thumbnail)
  - Apply `EditSettings` from `edit_settings` JSON:
    - Crop: decode full-res → crop region via pixel offset + dimensions
    - Exposure: multiply pixel values by `pow(2, exposure)`
    - Contrast/saturation/temperature: simple per-pixel adjustments (HSL-space for saturation, color matrix for temperature)
  - Write EXIF APP1 block into output JPEG:
    - `DateTimeOriginal` from `PhotoRecord.captureTime`
    - `Make` / `Model` from `PhotoRecord.cameraMake` / `cameraModel`
    - GPS IFD from `PhotoRecord.gpsLat` / `gpsLon` / `gpsAltM` (if non-zero)
    - Write using libjpeg-turbo `jpeg_write_marker` for APP1
  - Output filename: `{captureTime_date}_{original_filename_stem}.jpg`
  - Background `ThreadPool` (reuse existing), `progressCb_` per file, `doneCb_` on completion

  *main.mm:*
  - Wire "Export" button → `exportDialog_.open(primaryId, selectedIds)`
  - Toolbar "Export" button available when ≥1 photo selected (single-photo export works too)

  ✓ Verify: Select 10 RAW photos with various edits → Export → 10 JPEGs in target folder, each full-res with crop applied. `exiftool output.jpg` shows correct `DateTimeOriginal`, `Make`, `Model`. Photos import cleanly into Google Photos with correct dates.
