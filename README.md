# Photo Library

A native macOS RAW photo library manager built with C++20, Metal, and Dear ImGui.

## Features

- **Grid view** — thumbnail grid with virtual scroll (handles 100k+ photos at 60 fps)
- **Import** — recursive scan of any folder or USB drive; RAW + JPEG support
- **Dedup** — XXH3-based fast fingerprint; skips duplicate files automatically
- **Fullscreen view** — zoom, pan, arrow-key navigation, pick toggle
- **Export** — JPEG export with quality/size presets (High / Medium / Low)
- **Folder tree** — per-folder photo counts; All / Picked filter
- **Backup** — weekly automatic catalog backup with 5-rotation rolling delete

## Requirements

- macOS 13+ (Apple Silicon / arm64)
- Xcode 15+ with Command Line Tools
- [Homebrew](https://brew.sh)
- CMake 3.25+, Ninja

## Dependencies

All runtime dependencies are installed via Homebrew. ImGui is a git submodule.

| Library | Purpose |
|---|---|
| SDL2 | Windowing and events |
| LibRaw | RAW decoding and EXIF extraction |
| libjpeg-turbo | Thumbnail encode/decode, JPEG export |
| SQLite3 | Catalog database (WAL mode) |
| xxhash | XXH3 dedup fingerprinting |
| nlohmann/json | App config and edit settings |
| spdlog + fmt | Structured logging |
| Catch2 v3 | Unit tests |
| Dear ImGui | UI (docking branch, git submodule) |

## Setup

### 1. Install dependencies

```bash
brew install cmake ninja sdl2 libraw jpeg-turbo xxhash nlohmann-json spdlog catch2
```

### 2. Clone with submodules

```bash
git clone --recurse-submodules <repo-url>
cd photos
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build

```bash
# Configure (debug build)
cmake --preset debug

# Build
cmake --build build/debug

# Run
./build/debug/PhotoLibrary.app/Contents/MacOS/PhotoLibrary
```

For a release build:

```bash
cmake --preset release
cmake --build build/release
./build/release/PhotoLibrary.app/Contents/MacOS/PhotoLibrary
```

## Run Tests

```bash
ctest --preset debug --output-on-failure
```

## Usage

### Importing Photos

1. Click **Import…** in the toolbar.
2. Set the source folder (or plug in a USB camera drive — it is detected automatically).
3. Click **Start Import**. Progress is shown per file; duplicates are skipped.
4. Click **Cancel** at any time to stop mid-import.

### Browsing

- Use the **Folders** panel on the left to filter by folder.
- Toggle **All / Picked** in the filter bar to show only starred photos.
- Click a thumbnail to select it.

### Fullscreen View

| Key | Action |
|---|---|
| `F` | Open fullscreen for selected photo |
| `Escape` / `G` | Return to grid |
| `←` / `→` | Previous / next photo |
| `` ` `` (backtick) | Toggle Picked star |
| Scroll wheel | Zoom in / out |
| Drag | Pan |

### Exporting

1. Select a photo (or multiple via the Export button).
2. Click **Export Selected**.
3. Choose a preset (High Quality · Medium Quality · Low/Web) and a target folder.
4. Click **Export**.

| Preset | JPEG Quality | Max dimension |
|---|---|---|
| High Quality | 92 | unlimited |
| Medium Quality | 75 | 2048 px |
| Low / Web | 60 | 1024 px |

### Backup

The catalog is backed up automatically once a week to:

```
~/Library/Application Support/PhotoLibrary/backups/catalog_YYYYMMDD_HHMMSS.db
```

Only the 5 most recent backups are kept.

## File Locations

| Path | Contents |
|---|---|
| `~/Library/Application Support/PhotoLibrary/catalog.db` | SQLite catalog |
| `~/Library/Application Support/PhotoLibrary/backups/` | Weekly backups |
| `~/Library/Caches/PhotoLibrary/thumbs/` | JPEG thumbnails (`{xx}/{hash}.jpg`) |
| `~/Library/Logs/PhotoLibrary/` | Application logs |

## Architecture

```
src/
├── catalog/    Database, Schema, PhotoRepository, ThumbnailCache, BackupManager
├── import/     VolumeWatcher, FileScanner, RawDecoder, HashDedup, Importer
├── ui/         Renderer, TextureManager, GridView, FullscreenView, dialogs
├── export/     Exporter, ExportPreset
└── util/       Platform (macOS APIs), ThreadPool
```

Key design rules:

- **DB writes** on background thread only, guarded by `std::mutex`
- **Metal textures** use `MTLStorageModeShared` (zero-copy on Apple Silicon)
- **LRU texture cache** — 2000 slot cap; placeholder returned while async decode is in flight
- **Virtual scroll** — only renders rows in the viewport; handles 10k+ photos at 60 fps
- **Dedup** — XXH3-64 fast fingerprint first; full XXH3-128 only on collision
