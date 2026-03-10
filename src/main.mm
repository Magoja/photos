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

// UI
#include "ui/TextureManager.h"
#include "ui/GridView.h"
#include "ui/FolderTreePanel.h"
#include "ui/FilterBar.h"
#include "ui/FullscreenView.h"
#include "ui/ImportDialog.h"
#include "ui/ExportDialog.h"
#include "ui/SettingsPanel.h"

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
static CAMetalLayer* getMetalLayer(SDL_Window* window, id<MTLDevice> device)
{
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    NSWindow* nsWindow = wmInfo.info.cocoa.window;
    NSView*   view     = nsWindow.contentView;
    view.wantsLayer    = YES;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device        = device;
    layer.pixelFormat   = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    view.layer          = layer;
    return layer;
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/)
{
    // ── Logging ──────────────────────────────────────────────────────────────
    auto logger = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Jakeutil Photos starting");

    // ── Platform dirs ─────────────────────────────────────────────────────────
    std::string appSupport = util::appSupportDir();
    std::string cacheDir   = util::cacheDir();
    std::string thumbDir   = cacheDir + "/thumbs";
    std::string backupDir  = appSupport + "/backups";
    std::string dbPath     = appSupport + "/catalog.db";
    util::ensureDir(appSupport);
    util::ensureDir(backupDir);

    // ── Database ──────────────────────────────────────────────────────────────
    catalog::Database        db(dbPath);
    catalog::PhotoRepository repo(db);

    // Load library root before schema migration so v2 can strip the prefix
    std::string libraryRoot = repo.getSetting("library_root");
    catalog::Schema::apply(db, libraryRoot);
    repo.setLibraryRoot(libraryRoot);

    // Migrate old thumb_path entries that used the old app bundle identifier
    db.exec("UPDATE photos SET thumb_path = REPLACE(thumb_path, '/PhotoLibrary/', "
            "'/com.jakeutil.photos/') WHERE thumb_path LIKE '%/PhotoLibrary/%'");
    catalog::ThumbnailCache  thumbCache(thumbDir);
    catalog::BackupManager   backupMgr(db, dbPath, backupDir);

    // Backup check (non-blocking; quick if not due)
    {
        util::ThreadPool pool(1);
        pool.submit([&]{ backupMgr.checkAndBackup(); });
    }

    // ── SDL2 ──────────────────────────────────────────────────────────────────
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        spdlog::error("SDL_Init: {}", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

    const int W = 1280, H = 800;
    SDL_Window* window = SDL_CreateWindow(
        "Jakeutil Photos",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_METAL);
    if (!window) {
        spdlog::error("SDL_CreateWindow: {}", SDL_GetError());
        SDL_Quit(); return 1;
    }

    // ── Metal ─────────────────────────────────────────────────────────────────
    id<MTLDevice>       device   = MTLCreateSystemDefaultDevice();
    CAMetalLayer*       layer    = getMetalLayer(window, device);
    id<MTLCommandQueue> cmdQueue = [device newCommandQueue];

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0.12, 0.12, 0.12, 1.0);

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
    ui::GridView         grid(repo, texMgr);
    ui::FolderTreePanel  folderPanel(repo);
    ui::FilterBar        filterBar;
    ui::FullscreenView   fullscreen(repo, texMgr);
    ui::ImportDialog     importDlg(db);
    ui::ExportDialog     exportDlg(repo);
    ui::SettingsPanel    settingsPanel(repo, dbPath);

    // ── Async thumbnail loader ─────────────────────────────────────────────────
    struct ThumbResult { int64_t pid; std::vector<uint8_t> bytes; };
    std::mutex               thumbMtx;
    std::queue<ThumbResult>  thumbResQ;
    util::ThreadPool         thumbPool(2);

    grid.setThumbMissCallback([&](int64_t pid, std::string path) {
        if (path.empty()) return;
        thumbPool.submit([pid, path=std::move(path), &thumbMtx, &thumbResQ]() {
            std::ifstream f(path, std::ios::binary);
            if (!f) return;
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
            if (!bytes.empty()) {
                std::lock_guard lk(thumbMtx);
                thumbResQ.push({pid, std::move(bytes)});
            }
        });
    });

    // Initial load
    folderPanel.refresh();
    grid.loadFolder(0, ui::FilterMode::All);

    folderPanel.setOnSelect([&](int64_t fid) {
        grid.loadFolder(fid, filterBar.mode());
    });

    fullscreen.setPickChangedCallback([&](int64_t /*pid*/, int /*picked*/) {
        grid.reload();
    });

    importDlg.setDoneCallback([&]() {
        folderPanel.refresh();
        grid.reload();
    });

    // ── Volume watcher ────────────────────────────────────────────────────────
    import_ns::VolumeWatcher volWatcher;
    volWatcher.setMountedCallback([&](const import_ns::VolumeInfo& vol) {
        spdlog::info("Volume mounted uuid={} path={}", vol.uuid, vol.mountPath);
        std::lock_guard lk(db.mutex());
        catalog::VolumeRecord v;
        v.uuid      = vol.uuid;
        v.label     = vol.label;
        v.mountPath = vol.mountPath;
        repo.upsertVolume(v);
    });
    volWatcher.setUnmountedCallback([&](const std::string& uuid) {
        spdlog::info("Volume unmounted uuid={}", uuid);
    });
    volWatcher.start();

    // ── Render loop ───────────────────────────────────────────────────────────
    bool running = true;
    static constexpr float kStatusH = 28.f;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_q &&
                (event.key.keysym.mod & KMOD_GUI))
                running = false;
        }

        int drawW = 0, drawH = 0;
        SDL_Metal_GetDrawableSize(window, &drawW, &drawH);
        layer.drawableSize = CGSizeMake(drawW, drawH);

        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) continue;

            rpd.colorAttachments[0].texture = drawable.texture;
            id<MTLCommandBuffer> cmdBuf = [cmdQueue commandBuffer];

            ImGui_ImplMetal_NewFrame(rpd);
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            // ── Drain thumbnail results (main thread upload) ──────────────────
            {
                std::queue<ThumbResult> local;
                {
                    std::lock_guard lk(thumbMtx);
                    std::swap(local, thumbResQ);
                }
                while (!local.empty()) {
                    auto& r = local.front();
                    texMgr.upload(r.pid, r.bytes);
                    local.pop();
                }
            }

            // ── Handle F key to open fullscreen ──────────────────────────────
            if (ImGui::IsKeyPressed(ImGuiKey_F) &&
                !ImGui::GetIO().WantTextInput &&
                grid.selectedId() > 0 &&
                !fullscreen.isOpen()) {
                std::vector<int64_t> ids = repo.queryAll(false);
                fullscreen.setPhotoList(ids, grid.selectedId());
                fullscreen.open(grid.selectedId());
            }

            // ── Menu bar ─────────────────────────────────────────────────────
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Import...")) {
                        importDlg.open("", libraryRoot, thumbDir);
                    }
                    if (ImGui::MenuItem("Export Selected",
                                        nullptr, false,
                                        grid.selectedId() > 0)) {
                        exportDlg.open({grid.selectedId()});
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Settings...")) {
                        settingsPanel.open();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit", "Cmd+Q")) {
                        running = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    bool isAll    = (filterBar.mode() == ui::FilterMode::All);
                    bool isPicked = (filterBar.mode() == ui::FilterMode::Picked);
                    if (ImGui::MenuItem("All Photos",   nullptr, isAll)) {
                        filterBar.setMode(ui::FilterMode::All);
                        grid.loadFolder(folderPanel.selectedFolder(), ui::FilterMode::All);
                    }
                    if (ImGui::MenuItem("Picked Only", nullptr, isPicked)) {
                        filterBar.setMode(ui::FilterMode::Picked);
                        grid.loadFolder(folderPanel.selectedFolder(), ui::FilterMode::Picked);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            // ── First-launch modal: ask user to choose library root ───────────
            if (libraryRoot.empty()) ImGui::OpenPopup("Choose Library Folder");
            if (ImGui::BeginPopupModal("Choose Library Folder", nullptr,
                                        ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Choose a folder where Jakeutil Photos will store your images.");
                ImGui::Spacing();
                if (ImGui::Button("Browse...")) {
                    if (auto p = util::pickFolder()) {
                        libraryRoot = *p;
                        util::ensureDir(libraryRoot);
                        repo.setSetting("library_root", libraryRoot);
                        repo.setLibraryRoot(libraryRoot);
                    }
                }
                if (!libraryRoot.empty()) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            // ── DockSpace (leave room for status bar at bottom) ───────────────
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y - kStatusH});
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
            ImGui::Begin("DockHost", nullptr,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar();
            ImGui::DockSpace(ImGui::GetID("DS"),{},ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();

            // ── Left panel — folders ──────────────────────────────────────────
            ImGui::SetNextWindowSize({220, -1}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Folders");
            folderPanel.render();
            ImGui::End();

            // ── Main photos panel ─────────────────────────────────────────────
            ImGui::Begin("Photos");
            // Filter bar at top
            if (filterBar.render()) {
                grid.loadFolder(folderPanel.selectedFolder(), filterBar.mode());
            }
            ImGui::Separator();
            grid.render();
            ImGui::End();

            // ── Status bar (pinned to bottom) ─────────────────────────────────
            ImGui::SetNextWindowPos({vp->WorkPos.x,
                                     vp->WorkPos.y + vp->WorkSize.y - kStatusH});
            ImGui::SetNextWindowSize({vp->WorkSize.x, kStatusH});
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.f, 4.f});
            ImGui::Begin("##StatusBar", nullptr,
                ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize   |
                ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoDocking  |
                ImGuiWindowFlags_NoScrollbar  | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::PopStyleVar();
            ImGui::Text("Photos: %lld  |  FPS: %.0f",
                        (long long)grid.photoCount(), io.Framerate);
            ImGui::SameLine();
            float sliderW = 150.f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - sliderW);
            ImGui::SetNextItemWidth(sliderW);
            float zoom = grid.thumbScale();
            if (ImGui::SliderFloat("##zoom", &zoom, 0.5f, 3.0f, ""))
                grid.setThumbScale(zoom);
            ImGui::End();

            // ── Settings panel ────────────────────────────────────────────────
            settingsPanel.render();

            // ── Fullscreen overlay ────────────────────────────────────────────
            fullscreen.render();

            // ── Dialogs ───────────────────────────────────────────────────────
            importDlg.render();
            exportDlg.render();

            ImGui::Render();

            id<MTLRenderCommandEncoder> enc =
                [cmdBuf renderCommandEncoderWithDescriptor:rpd];
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
