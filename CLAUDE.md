# Photo Library Manager

## How to continue
Say "Continue the project" to pick up the next unchecked task below.
Claude will: read this file → implement the next `- [ ]` task → run verification → **run unit tests** (`ctest --preset debug --output-on-failure`) → **create a git commit** → check it off `- [x]` → stop.

## Bug / crash fixes
When fixing a crash or bug: implement the fix, build, run unit tests, and explain the change — but **do NOT commit** until the user confirms the fix works.

## Stack
- C++20 + Obj-C++ (.mm) for macOS APIs (Metal, DiskArbitration, NSOpenPanel)
- Dear ImGui (git submodule `third_party/imgui`, docking branch) + SDL2 + Metal backend
- LibRaw 0.21 (RAW decode + embedded JPEG thumb extraction)
- libjpeg-turbo (thumbnail encode/decode, export JPEG)
- SQLite3 raw C API (WAL mode, prepared statement pool)
- xxhash XXH3 (dedup fingerprinting)
- nlohmann/json (app config + `edit_settings` column)
- spdlog (logging to `~/Library/Logs/PhotoLibrary/`)
- CMake 3.25 + CMakePresets.json (`debug` / `release`, arm64); **C++23**
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
- **Designated initializers**: always use C++20 designated initializer syntax (`.field = value`) when constructing structs by aggregate initialization. Never rely on positional order.
- **`std::expected` for fallible results**: use `std::expected<T, std::string>` (C++23) instead of a `{bool ok, string error}` struct for any function that can fail. Return values via the type directly; signal errors with `std::unexpected(msg)`. Provide thin `success()`/`failure()` free-function helpers in the same namespace when the call sites benefit from the symmetry.

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

9 (Grid) ──── [16✓ Multi-select] ─┬─ [17✓ Meta Sync]
                                   └─ [18✓ Export v2]
                                       └─ C1→C7 (Command System)
```

## Tasks

- [ ] **Task C1 — Command system foundation**
  Files: `src/command/CommandResult.h`, `src/command/ICommandHandler.h`,
  `src/command/CommandRegistry.h/.cpp`, `tests/test_command_registry.cpp`,
  update `CMakeLists.txt` (root + tests/)

  - `CommandResult { bool ok; std::string error; nlohmann::json data; }` with `success()`/`failure()` factories
  - `ICommandHandler`: pure abstract `virtual CommandResult execute(nlohmann::json params) = 0`
  - `CommandRegistry`: `registerHandler(name, handler)`, `dispatch(name, params)` — logs every call via spdlog as `[CMD] <name> -> ok/error`, returns `failure("unknown command: <name>")` for unregistered names
  - Add `COMMAND_SOURCES` to root CMakeLists; add `test_command` target to tests/CMakeLists.txt
  - Tests: dispatch unknown → failure with name in error; dispatch known → handler called with params; multiple handlers independent

  ✓ Verify: `ctest --preset debug -R test_command` passes.

- [ ] **Task C2 — `image.adjust` and `image.revert` handlers**
  Files: `src/command/handlers/ImageAdjustHandler.h/.cpp`,
  `src/command/handlers/ImageRevertHandler.h/.cpp`, `tests/test_command_image.cpp`

  - `ImageAdjustHandler(PhotoRepository&)`: reads current EditSettings via `findById`, overlays only the JSON keys present in params (exposure, temperature, contrast, saturation), writes back via `updateEditSettings`
  - `ImageRevertHandler(PhotoRepository&)`: writes `"{}"` to `edit_settings`
  - Tests (TempDb fixture): adjust exposure → verified by findById; partial params leave other fields untouched; missing id → failure; revert clears settings

  ✓ Verify: `ctest --preset debug -R test_command` passes.

- [x] **Task C3 — `image.crop` and `image.save` handlers**
  Files: `src/command/handlers/ImageCropHandler.h/.cpp`,
  `src/command/handlers/ImageSaveHandler.h/.cpp`, extend `tests/test_command_image.cpp`

  - `ImageCropHandler(PhotoRepository&)`: reads current EditSettings, overwrites crop sub-fields only (x, y, w, h, angleDeg), writes back — leaves exposure/temperature/etc untouched
  - `ImageSaveHandler(PhotoRepository&, std::function<void(int64_t)> savedCb)`: writes full EditSettings JSON blob, fires savedCb
  - Tests: crop writes only crop fields; save round-trips JSON blob; save fires savedCb (verified via bool flag)

  ✓ Verify: `ctest --preset debug -R test_command` passes.

- [ ] **Task C4 — `catalog.pick` and `catalog.photo.open` handlers**
  Files: `src/command/handlers/CatalogPickHandler.h/.cpp`,
  `src/command/handlers/CatalogOpenHandler.h/.cpp`, `tests/test_command_catalog.cpp`

  - `CatalogPickHandler(PhotoRepository&, std::function<void(int64_t, int)>)`: calls `updatePicked(id, picked)`, fires callback
  - `CatalogOpenHandler(std::function<void(int64_t)>)`: fires selectCb only, no DB access
  - Tests: pick updates DB `picked` column (verified by `queryByFolder(fid, true)`); callback fired with correct args; open fires selectCb

  ✓ Verify: `ctest --preset debug -R test_command` passes.

- [ ] **Task C5 — `metasync.apply` handler**
  Files: `src/command/handlers/MetaSyncHandler.h/.cpp`, `tests/test_command_metasync.cpp`

  - `MetaSyncHandler(PhotoRepository&, std::function<void()> doneCb)`
  - Extract merge logic from `MetaSyncDialog::performSync()` into this handler; MetaSyncDialog becomes a thin caller
  - Params: `{primaryId, targetIds: [int64], syncAdjust: bool, syncCrop: bool}`
  - Single DB transaction via `updateEditSettingsBulk`; fire doneCb after commit
  - Tests: syncAdjust only → exposure propagated, crop unchanged; syncCrop only; both; doneCb fires exactly once

  ✓ Verify: `ctest --preset debug -R test_command` passes.

- [ ] **Task C6 — `export.photos` handler**
  Files: `src/command/handlers/ExportHandler.h/.cpp`

  - `ExportHandler(PhotoRepository&, ProgressCb, DoneCb)`: owns `std::unique_ptr<Exporter>`
  - `execute` params: `{primaryId, ids: [int64], targetPath, quality? (default 90)}`
  - Constructs `ExportPreset`, calls `exporter_->start(ids)`, returns `success()` immediately
  - Returns `failure()` if an export is already in progress
  - No unit test (LibRaw required); verified by build + smoke run

  ✓ Verify: build succeeds; manual export in app produces JPEG files with correct EXIF.

- [ ] **Task C7 — Wire registry into main.mm + replace direct calls**
  Files: `src/main.mm`, `src/ui/EditView.mm`, `src/ui/ExportDialog.cpp`,
  `src/ui/MetaSyncDialog.cpp`, `src/ui/FullscreenView.cpp`

  - Add `command::CommandRegistry` to `RenderCtx`
  - Register all 8 handlers at app startup with real deps (repo, thumbCache, callbacks)
  - Replace direct calls in UI components with `registry.dispatch("command.name", params)`
  - Every action emits `[CMD] <name> <params>` in spdlog log

  ✓ Verify: launch app; perform adjust, save, crop, pick, export, sync — spdlog shows `[CMD]` line for each action. All existing functionality unchanged.
