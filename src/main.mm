// main.mm — Jakeutil Photos entry point
// Metal + SDL2 + ImGui application shell

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_metal.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Catalog
#include "catalog/Database.h"
#include "catalog/Schema.h"
#include "catalog/PhotoRepository.h"
#include "catalog/ThumbnailCache.h"
#include "catalog/BackupManager.h"

// Import
#include "import/VolumeWatcher.h"
#include "import/Importer.h"
#include "import/RawDecoder.h"

// UI
#include "ui/TextureManager.h"
#include "ui/GridView.h"
#include "ui/FolderTreePanel.h"
#include "ui/FilterBar.h"
#include "ui/FullscreenView.h"
#include "ui/EditView.h"
#include "ui/ImportDialog.h"
#include "ui/ExportDialog.h"
#include "ui/MetaSyncDialog.h"
#include "ui/SettingsPanel.h"

// Command system
#include "command/CommandRegistry.h"
#include "command/AppCommands.h"
#include "export/ExportSession.h"

// Util
#include "util/Platform.h"
#include "util/ThreadPool.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>

namespace fs = std::filesystem;

// ── Metal layer helper ────────────────────────────────────────────────────────
static CAMetalLayer* getMetalLayer(SDL_Window* window, id<MTLDevice> device) {
  SDL_SysWMinfo wmInfo{};
  SDL_VERSION(&wmInfo.version);
  SDL_GetWindowWMInfo(window, &wmInfo);
  NSWindow* nsWindow = wmInfo.info.cocoa.window;
  NSView* view = nsWindow.contentView;
  view.wantsLayer = YES;

  CAMetalLayer* layer = [CAMetalLayer layer];
  layer.device = device;
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.framebufferOnly = YES;
  view.layer = layer;
  return layer;
}

// ── Free helpers (anonymous namespace) ───────────────────────────────────────
namespace {

static constexpr float kStatusH = 28.f;

struct ThumbResult {
  int64_t pid;
  std::vector<uint8_t> rgba;
  int width = 0;
  int height = 0;
};

struct RenderCtx {
  bool& running;
  std::string& libraryRoot;
  std::string& thumbDir;
  catalog::Database& db;
  catalog::PhotoRepository& repo;
  catalog::ThumbnailCache& thumbCache;
  ui::TextureManager& texMgr;
  ui::GridView& grid;
  ui::FolderTreePanel& folderPanel;
  ui::FilterBar& filterBar;
  ui::FullscreenView& fullscreen;
  ui::EditView& editView;
  ui::ImportDialog& importDlg;
  ui::ExportDialog& exportDlg;
  ui::MetaSyncDialog& metaSyncDlg;
  ui::SettingsPanel& settingsPanel;
  std::mutex& thumbMtx;
  std::queue<ThumbResult>& thumbResQ;
  const command::CommandRegistry& registry;
  // Deferred clear-cache: set by Settings button, executed at the start of the
  // next frame before any draw calls to avoid freeing textures mid-frame.
  bool clearCachePending = false;
};

static bool loadAndDecodeJpeg(const std::string& path,
                               std::vector<uint8_t>& outRgba, int& outW, int& outH) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { return false; }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
  if (bytes.empty()) { return false; }
  return ui::TextureManager::decodeJpeg(bytes, outRgba, outW, outH);
}

static std::vector<int64_t> buildSelectionList(
    const std::unordered_set<int64_t>& selected, int64_t primaryId) {
  std::vector<int64_t> ids(selected.begin(), selected.end());
  ids.push_back(primaryId);
  return ids;
}

static void openOrSwitchEditMode(ui::FullscreenView& fullscreen,
                                  ui::EditView& editView,
                                  int64_t selId,
                                  ui::EditMode mode) {
  const int64_t target = fullscreen.isOpen() ? fullscreen.currentId() : selId;
  fullscreen.close();
  if (editView.isOpen()) {
    editView.setMode(mode);
  } else {
    editView.open(target);
    editView.setMode(mode);
  }
}

static void applyFilterMode(ui::FilterBar& filterBar, ui::GridView& grid,
                             ui::FolderTreePanel& folderPanel,
                             catalog::PhotoRepository& repo, ui::FilterMode mode) {
  filterBar.setMode(mode);
  grid.loadFolder(folderPanel.selectedFolder(), mode);
  repo.setSetting("last_filter_mode", std::to_string(static_cast<int>(mode)));
}

static void setupThumbMissCallback(RenderCtx& ctx, util::ThreadPool& thumbPool) {
  ctx.grid.setThumbMissCallback(
    [&](int64_t pid, std::string path, std::string microPath) {
      thumbPool.submit([pid, path = std::move(path), microPath = std::move(microPath),
                        &ctx]() {
        // Micro load (fast path — tiny file, nearly always cache-hit after first import)
        if (!microPath.empty()) {
          std::vector<uint8_t> rgba;
          int w = 0, h = 0;
          if (loadAndDecodeJpeg(microPath, rgba, w, h)) {
            std::lock_guard lk(ctx.thumbMtx);
            ctx.thumbResQ.push({pid + ui::GridView::kMicroOffset, std::move(rgba), w, h});
          }
        }

        // Standard load: fast path if file already cached on disk
        if (!path.empty()) {
          std::vector<uint8_t> rgba;
          int w = 0, h = 0;
          if (loadAndDecodeJpeg(path, rgba, w, h)) {
            std::lock_guard lk(ctx.thumbMtx);
            ctx.thumbResQ.push({pid, std::move(rgba), w, h});
            return;
          }
          // Path was set but file missing or unreadable — do NOT fall through
          // to slow path: that would overwrite an edited thumbnail's DB entry
          // with the original hash-based path, silently reverting edits.
          spdlog::warn("Thumb load failed for pid={}: {}", pid, path);
          return;
        }

        // Slow path: no thumb path in DB — decode source and generate for the first time
        auto rec = [&]() -> std::optional<catalog::PhotoRecord> {
          std::lock_guard lk(ctx.db.mutex());
          return ctx.repo.findById(pid);
        }();
        if (!rec) {
          return;
        }

        std::string srcPath = ctx.repo.fullPathFor(rec->folderId, rec->filename);
        auto dec = import_ns::RawDecoder::decode(srcPath);
        if (!dec.ok || dec.thumbJpeg.empty()) {
          return;
        }

        {
          std::lock_guard lk(ctx.db.mutex());
          ctx.thumbCache.generate(pid, rec->fileHash, dec.thumbJpeg, ctx.repo, dec.lumaScale);
          ctx.thumbCache.generateMicro(pid, rec->fileHash, dec.thumbJpeg, ctx.repo, dec.lumaScale);
        }

        // Now serve the freshly written standard file
        std::string newPath = ctx.repo.getThumbPath(pid);
        if (newPath.empty()) {
          return;
        }
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (loadAndDecodeJpeg(newPath, rgba, w, h)) {
          std::lock_guard lk(ctx.thumbMtx);
          ctx.thumbResQ.push({pid, std::move(rgba), w, h});
        }
      });
    });
}

static void wireUiCallbacks(RenderCtx& ctx) {
  ctx.folderPanel.setOnSelect([&](int64_t fid) {
    ctx.grid.loadFolder(fid, ctx.filterBar.mode());
    ctx.repo.setSetting("last_folder_id", std::to_string(fid));
  });

  // catalog.pick callback: reload grid after pick state changes.
  ctx.fullscreen.setPickChangedCallback(
    [&](int64_t /*pid*/, int /*picked*/) { ctx.grid.reload(); });

  ctx.fullscreen.setOpenEditCallback([&](const int64_t photoId) {
    if (!ctx.editView.isOpen()) {
      ctx.editView.open(photoId);
    }
  });
  ctx.fullscreen.setRegistry(&ctx.registry);

  ctx.editView.setSavedCallback([&](int64_t /*photoId*/) { ctx.grid.reload(); });
  ctx.editView.setRegistry(&ctx.registry);

  ctx.importDlg.setDoneCallback([&]() {
    ctx.folderPanel.refresh();
    ctx.grid.reload();
  });

  ctx.metaSyncDlg.setDoneCallback([&]() { ctx.grid.reload(); });
  ctx.metaSyncDlg.setRegistry(&ctx.registry);

  // NOTE: set a deferred flag rather than calling evictAll() immediately.
  // The Settings panel renders after the grid has already added texture pointers
  // to the ImGui draw list — calling evictAll() mid-frame would free those
  // textures before ImGui_ImplMetal_RenderDrawData processes them (crash).
  ctx.settingsPanel.setClearCacheCallback([&]() {
    ctx.clearCachePending = true;
  });
}

static void drainThumbQueue(RenderCtx& ctx) {
  std::queue<ThumbResult> local;
  {
    std::lock_guard lk(ctx.thumbMtx);
    std::swap(local, ctx.thumbResQ);
  }
  while (!local.empty()) {
    auto& r = local.front();
    ctx.texMgr.uploadRgba(r.pid, r.rgba, r.width, r.height);
    local.pop();
  }
}

static void drainClearCache(RenderCtx& ctx) {
  if (!ctx.clearCachePending) { return; }
  ctx.clearCachePending = false;
  ctx.texMgr.evictAll();
  {
    std::lock_guard lk(ctx.db.mutex());
    ctx.repo.clearAllThumbs();
  }
  const std::string cacheBase = util::cacheDir();
  for (const auto& dir : {ctx.thumbDir,
                           cacheBase + "/thumbs_micro",
                           cacheBase + "/thumbs_edit"}) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
  }
  ctx.grid.reload();
}

static void drainTextureEvictions(RenderCtx& ctx) {
  if (const int64_t evictId = ctx.editView.pollPendingEvict(); evictId > 0) {
    ctx.texMgr.evict(evictId);
    ctx.texMgr.evict(evictId + ui::GridView::kMicroOffset);
  }
  for (auto& u : ctx.metaSyncDlg.takePendingThumbUpdates()) {
    ctx.texMgr.evict(u.id);
    ctx.texMgr.evict(u.id + ui::GridView::kMicroOffset);
    ctx.texMgr.uploadRgba(u.id, u.rgba, u.w, u.h);
  }
}

static void togglePickSelection(RenderCtx& ctx) {
  const int64_t selId = ctx.grid.selectedId();
  if (selId <= 0) { return; }
  const auto rec = ctx.repo.findById(selId);
  if (!rec) { return; }
  const int newPicked = rec->picked ? 0 : 1;

  // Capture IDs before dispatching — each dispatch triggers grid.reload()
  std::vector<int64_t> toToggle{selId};
  for (const auto id : ctx.grid.selectedIds()) { toToggle.push_back(id); }

  for (const auto id : toToggle) {
    ctx.registry.dispatch("catalog.pick", {{"id", id}, {"picked", newPicked}});
  }
}

static void processGlobalHotkeys(RenderCtx& ctx) {
  const auto& io = ImGui::GetIO();
  if (io.WantTextInput) { return; }

  // Cmd+A: select all photos in the current view (works even with no current selection)
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
    ctx.grid.selectAll();
  }

  // ESC: cancel selection (only when grid is active, not fullscreen/edit)
  if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
      !ctx.fullscreen.isOpen() && !ctx.editView.isOpen() &&
      ctx.grid.selectionCount() > 0) {
    ctx.grid.clearSelection();
  }

  const int64_t selId = ctx.grid.selectedId();
  if (selId <= 0) { return; }

  if (ImGui::IsKeyPressed(ImGuiKey_F) && !ctx.fullscreen.isOpen()) {
    ctx.editView.close();
    std::vector<int64_t> ids = ctx.repo.queryAll(false);
    ctx.fullscreen.setPhotoList(ids, selId);
    ctx.fullscreen.open(selId);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_D)) {
    openOrSwitchEditMode(ctx.fullscreen, ctx.editView, selId, ui::EditMode::Adjust);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_R)) {
    openOrSwitchEditMode(ctx.fullscreen, ctx.editView, selId, ui::EditMode::Crop);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent) && !ctx.fullscreen.isOpen()) { togglePickSelection(ctx); }
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))   { ctx.grid.navigatePrimary(-1); }
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))  { ctx.grid.navigatePrimary(+1); }
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))     { ctx.grid.navigatePrimary(-ctx.grid.columnCount()); }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))   { ctx.grid.navigatePrimary(+ctx.grid.columnCount()); }
}

static void renderMenuBar(RenderCtx& ctx) {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Import...")) {
      ctx.importDlg.open("", ctx.libraryRoot, ctx.thumbDir);
    }
    if (ImGui::MenuItem("Export Selected", nullptr, false, ctx.grid.primaryId() > 0)) {
      ctx.exportDlg.open(ctx.grid.primaryId(),
                         buildSelectionList(ctx.grid.selectedIds(), ctx.grid.primaryId()));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Settings...")) {
      ctx.settingsPanel.open();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "Cmd+Q")) {
      ctx.running = false;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    const bool isAll    = (ctx.filterBar.mode() == ui::FilterMode::All);
    const bool isPicked = (ctx.filterBar.mode() == ui::FilterMode::Picked);
    if (ImGui::MenuItem("All Photos", nullptr, isAll)) {
      applyFilterMode(ctx.filterBar, ctx.grid, ctx.folderPanel, ctx.repo, ui::FilterMode::All);
    }
    if (ImGui::MenuItem("Picked Only", nullptr, isPicked)) {
      applyFilterMode(ctx.filterBar, ctx.grid, ctx.folderPanel, ctx.repo, ui::FilterMode::Picked);
    }
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

static void renderLibraryRootModal(RenderCtx& ctx) {
  if (ctx.libraryRoot.empty()) {
    ImGui::OpenPopup("Choose Library Folder");
  }
  if (!ImGui::BeginPopupModal("Choose Library Folder", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::Text("Choose a folder where Jakeutil Photos will store your images.");
  ImGui::Spacing();
  if (ImGui::Button("Browse...")) {
    if (auto p = util::pickFolder()) {
      ctx.libraryRoot = *p;
      util::ensureDir(ctx.libraryRoot);
      ctx.repo.setSetting("library_root", ctx.libraryRoot);
      ctx.repo.setLibraryRoot(ctx.libraryRoot);
    }
  }
  if (!ctx.libraryRoot.empty()) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

static void renderPhotosPanel(RenderCtx& ctx) {
  ImGui::Begin("Photos");
  if (ctx.filterBar.render()) {
    ctx.grid.loadFolder(ctx.folderPanel.selectedFolder(), ctx.filterBar.mode());
    ctx.repo.setSetting("last_filter_mode",
                        std::to_string(static_cast<int>(ctx.filterBar.mode())));
  }
  if (ctx.grid.selectionCount() >= 2) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.f));
    if (ImGui::Button("Sync Metadata")) {
      ctx.metaSyncDlg.open(ctx.grid.primaryId(),
                           buildSelectionList(ctx.grid.selectedIds(), ctx.grid.primaryId()));
    }
    ImGui::PopStyleColor();
  }
  if (ctx.grid.primaryId() > 0) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.f));
    if (ImGui::Button("Export")) {
      ctx.exportDlg.open(ctx.grid.primaryId(),
                         buildSelectionList(ctx.grid.selectedIds(), ctx.grid.primaryId()));
    }
    ImGui::PopStyleColor();
  }
  ImGui::Separator();
  ctx.grid.render();
  ImGui::End();
}

static std::string formatFileSize(int64_t bytes) {
  if (bytes <= 0) { return {}; }
  char buf[32];
  if (bytes >= 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
  } else {
    std::snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
  }
  return buf;
}

static std::string buildPhotoMetaString(const catalog::PhotoRecord& rec) {
  std::vector<std::string> fields;
  fields.push_back(rec.filename);

  if (rec.captureTime.size() >= 10) {
    fields.push_back(rec.captureTime.substr(0, 10));
  }

  if (!rec.cameraMake.empty() || !rec.cameraModel.empty()) {
    fields.push_back(rec.cameraMake + ((!rec.cameraMake.empty() && !rec.cameraModel.empty()) ? " " : "") + rec.cameraModel);
  }

  if (rec.widthPx > 0 && rec.heightPx > 0) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d x %d", rec.widthPx, rec.heightPx);
    fields.push_back(buf);
  }

  const std::string sz = formatFileSize(rec.fileSize);
  if (!sz.empty()) { fields.push_back(sz); }

  std::string result;
  for (const auto& f : fields) {
    if (!result.empty()) { result += "   |   "; }
    result += f;
  }
  return result;
}

static void renderStatusBar(RenderCtx& ctx) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - kStatusH});
  ImGui::SetNextWindowSize({vp->WorkSize.x, kStatusH});
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.f, 4.f});
  ImGui::Begin("##StatusBar", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();

  // Left: photo count
  ImGui::Text("Photos: %lld", (long long)ctx.grid.photoCount());

  // Center: selected photo metadata
  const int64_t selId = ctx.grid.primaryId();
  if (selId > 0) {
    const auto rec = ctx.repo.findById(selId);
    if (rec) {
      ImGui::SameLine(0.f, 16.f);
      ImGui::TextDisabled("%s", buildPhotoMetaString(*rec).c_str());
    }
  }

  // Right: zoom slider
  const float sliderW = 150.f;
  ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - sliderW);
  ImGui::SetNextItemWidth(sliderW);
  float zoom = ctx.grid.thumbScale();
  if (ImGui::SliderFloat("##zoom", &zoom, 0.5f, 3.0f, "")) {
    ctx.grid.setThumbScale(zoom);
  }
  ImGui::End();
}

} // namespace

// ── Main ──────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/) {
  // ── Logging ──────────────────────────────────────────────────────────────
  auto logger = spdlog::stdout_color_mt("console");
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::info);
  spdlog::info("Jakeutil Photos starting");

  // ── Platform dirs ─────────────────────────────────────────────────────────
  std::string appSupport = util::appSupportDir();
  std::string cacheDir = util::cacheDir();
  std::string thumbDir = cacheDir + "/thumbs";
  std::string backupDir = appSupport + "/backups";
  std::string dbPath = appSupport + "/catalog.db";
  util::ensureDir(appSupport);
  util::ensureDir(backupDir);

  // ── Database ──────────────────────────────────────────────────────────────
  catalog::Database db(dbPath);
  catalog::PhotoRepository repo(db);

  // Load library root before schema migration so v2 can strip the prefix
  std::string libraryRoot = repo.getSetting("library_root");
  catalog::Schema::apply(db, libraryRoot);
  repo.setLibraryRoot(libraryRoot);

  // Migrate old thumb_path entries that used the old app bundle identifier
  db.exec("UPDATE photos SET thumb_path = REPLACE(thumb_path, '/PhotoLibrary/', "
          "'/com.jakeutil.photos/') WHERE thumb_path LIKE '%/PhotoLibrary/%'");
  catalog::ThumbnailCache thumbCache(thumbDir);
  catalog::BackupManager backupMgr(db, dbPath, backupDir);

  // Backup check (non-blocking; quick if not due)
  {
    util::ThreadPool pool(1);
    pool.submit([&] { backupMgr.checkAndBackup(); });
  }

  // ── SDL2 ──────────────────────────────────────────────────────────────────
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");   // suppress HID gamepad probing on macOS
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    spdlog::error("SDL_Init: {}", SDL_GetError());
    return 1;
  }

  const int W = 1280, H = 800;
  SDL_Window* window =
    SDL_CreateWindow("Jakeutil Photos", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_METAL);
  if (!window) {
    spdlog::error("SDL_CreateWindow: {}", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  // ── Metal ─────────────────────────────────────────────────────────────────
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  CAMetalLayer* layer = getMetalLayer(window, device);
  id<MTLCommandQueue> cmdQueue = [device newCommandQueue];

  MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
  rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
  rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
  rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.12, 0.12, 0.12, 1.0);

  // ── ImGui ─────────────────────────────────────────────────────────────────
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  static std::string imguiIniPath = appSupport + "/imgui.ini";
  io.IniFilename = imguiIniPath.c_str();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForMetal(window);
  ImGui_ImplMetal_Init(device);

  // ── TextureManager ────────────────────────────────────────────────────────
  ui::TextureManager texMgr;
  texMgr.init((MTLDevicePtr)device);

  // ── UI components ─────────────────────────────────────────────────────────
  ui::GridView grid(repo, texMgr);
  ui::FolderTreePanel folderPanel(repo);
  ui::FilterBar filterBar;
  ui::FullscreenView fullscreen(repo, texMgr);
  ui::EditView editView(repo, thumbCache, texMgr, (MTLDevicePtr)device);
  ui::ImportDialog importDlg(db, texMgr);
  export_ns::ExportSession exportSession(repo);
  ui::ExportDialog exportDlg(repo, exportSession);
  ui::MetaSyncDialog metaSyncDlg(repo, texMgr);
  ui::SettingsPanel settingsPanel(repo, dbPath);

  // ── Async thumbnail loader ─────────────────────────────────────────────────
  std::mutex thumbMtx;
  std::queue<ThumbResult> thumbResQ;
  util::ThreadPool thumbPool(2);

  // ── Command registry ──────────────────────────────────────────────────────
  const command::CommandRegistry registry =
      command::buildRegistry(repo, texMgr, grid, exportSession);

  // ── Wire everything up ────────────────────────────────────────────────────
  bool running = true;
  RenderCtx ctx{running, libraryRoot, thumbDir, db, repo, thumbCache, texMgr,
                grid, folderPanel, filterBar, fullscreen, editView,
                importDlg, exportDlg, metaSyncDlg, settingsPanel,
                thumbMtx, thumbResQ, registry};

  folderPanel.refresh();
  // Restore last session state
  const int64_t lastFolder = static_cast<int64_t>(
      std::stoll(repo.getSetting("last_folder_id", "0")));
  const ui::FilterMode lastFilter =
      std::stoi(repo.getSetting("last_filter_mode", "0")) == 1
          ? ui::FilterMode::Picked
          : ui::FilterMode::All;
  folderPanel.setSelectedFolder(lastFolder);
  filterBar.setMode(lastFilter);
  grid.loadFolder(lastFolder, lastFilter);

  setupThumbMissCallback(ctx, thumbPool);
  wireUiCallbacks(ctx);

  // ── Volume watcher ────────────────────────────────────────────────────────
  import_ns::VolumeWatcher volWatcher;
  volWatcher.setMountedCallback([&](const import_ns::VolumeInfo& vol) {
    spdlog::info("Volume mounted uuid={} path={}", vol.uuid, vol.mountPath);
    std::lock_guard lk(db.mutex());
    catalog::VolumeRecord v;
    v.uuid = vol.uuid;
    v.label = vol.label;
    v.mountPath = vol.mountPath;
    repo.upsertVolume(v);
  });
  volWatcher.setUnmountedCallback(
    [&](const std::string& uuid) { spdlog::info("Volume unmounted uuid={}", uuid); });
  volWatcher.start();

  // ── Render loop ───────────────────────────────────────────────────────────
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        running = false;
      }
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q &&
          (event.key.keysym.mod & KMOD_GUI)) {
        running = false;
      }
    }

    int drawW = 0, drawH = 0;
    SDL_Metal_GetDrawableSize(window, &drawW, &drawH);
    layer.drawableSize = CGSizeMake(drawW, drawH);

    @autoreleasepool {
      id<CAMetalDrawable> drawable = [layer nextDrawable];
      if (!drawable) {
        continue;
      }

      rpd.colorAttachments[0].texture = drawable.texture;
      id<MTLCommandBuffer> cmdBuf = [cmdQueue commandBuffer];

      ImGui_ImplMetal_NewFrame(rpd);
      ImGui_ImplSDL2_NewFrame();
      ImGui::NewFrame();

      drainClearCache(ctx);
      drainThumbQueue(ctx);
      drainTextureEvictions(ctx);
      processGlobalHotkeys(ctx);
      renderMenuBar(ctx);
      renderLibraryRootModal(ctx);

      // ── DockSpace (leave room for status bar at bottom) ───────────────
      ImGuiViewport* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y - kStatusH});
      ImGui::SetNextWindowViewport(vp->ID);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
      ImGui::Begin("DockHost", nullptr,
                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground);
      ImGui::PopStyleVar();
      ImGui::DockSpace(ImGui::GetID("DS"), {}, ImGuiDockNodeFlags_PassthruCentralNode);
      ImGui::End();

      // ── Left panel — folders ──────────────────────────────────────────
      ImGui::SetNextWindowSize({220, -1}, ImGuiCond_FirstUseEver);
      ImGui::Begin("Folders");
      folderPanel.render();
      ImGui::End();

      renderPhotosPanel(ctx);
      renderStatusBar(ctx);

      fullscreen.render();
      editView.render();
      importDlg.render();
      exportDlg.render();
      metaSyncDlg.render();
      settingsPanel.render();

      ImGui::Render();

      id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:rpd];
      ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmdBuf, enc);
      [enc endEncoding];
      [cmdBuf presentDrawable:drawable];
      [cmdBuf commit];
    }
  }

  // ── Cleanup ───────────────────────────────────────────────────────────────
  volWatcher.stop();
  spdlog::info("Shutting down");
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
