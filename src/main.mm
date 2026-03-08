// main.mm — Photo Library entry point
// Metal + SDL2 + ImGui minimal scaffold (Task 1)

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

#include <cstdio>
#include <cstdlib>

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────
static CAMetalLayer* getMetalLayer(SDL_Window* window)
{
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    NSWindow* nsWindow = wmInfo.info.cocoa.window;
    NSView*   view     = nsWindow.contentView;
    view.wantsLayer    = YES;

    CAMetalLayer* layer = [CAMetalLayer layer];
    view.layer          = layer;
    return layer;
}

// ────────────────────────────────────────────────────────────────────────────
// main
// ────────────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/)
{
    // ── Logging ─────────────────────────────────────────────────────────────
    auto logger = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Photo Library starting");

    // ── SDL2 ─────────────────────────────────────────────────────────────────
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        spdlog::error("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

    const int W = 1280, H = 800;
    SDL_Window* window = SDL_CreateWindow(
        "Photo Library",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_METAL
    );
    if (!window) {
        spdlog::error("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // ── Metal ────────────────────────────────────────────────────────────────
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        spdlog::error("No Metal device");
        return 1;
    }

    CAMetalLayer* layer        = getMetalLayer(window);
    layer.device               = device;
    layer.pixelFormat          = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly      = YES;

    id<MTLCommandQueue> cmdQueue = [device newCommandQueue];

    // ── ImGui ────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForMetal(window);
    ImGui_ImplMetal_Init(device);

    // Render pass descriptor reused each frame
    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0.12, 0.12, 0.12, 1.0);

    spdlog::info("Window ready — entering render loop");

    bool running = true;
    while (running) {
        // ── Events ──────────────────────────────────────────────────────────
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
            // Cmd+Q
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_q &&
                (event.key.keysym.mod & KMOD_GUI)) {
                running = false;
            }
        }

        // ── Resize layer ────────────────────────────────────────────────────
        int drawW = 0, drawH = 0;
        SDL_Metal_GetDrawableSize(window, &drawW, &drawH);
        layer.drawableSize = CGSizeMake(drawW, drawH);

        // ── Acquire drawable ─────────────────────────────────────────────────
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) continue;

            rpd.colorAttachments[0].texture = drawable.texture;

            id<MTLCommandBuffer> cmdBuf = [cmdQueue commandBuffer];

            // ── ImGui frame ─────────────────────────────────────────────────
            ImGui_ImplMetal_NewFrame(rpd);
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            // Main dockspace
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGuiWindowFlags dsFlags =
                ImGuiWindowFlags_NoDocking     |
                ImGuiWindowFlags_NoTitleBar    |
                ImGuiWindowFlags_NoCollapse    |
                ImGuiWindowFlags_NoResize      |
                ImGuiWindowFlags_NoMove        |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus    |
                ImGuiWindowFlags_NoBackground;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            ImGui::Begin("DockSpaceHost", nullptr, dsFlags);
            ImGui::PopStyleVar();
            ImGuiID dsId = ImGui::GetID("MainDockSpace");
            ImGui::DockSpace(dsId, {0, 0}, ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();

            // Status overlay
            ImGui::SetNextWindowPos({10, 10});
            ImGui::SetNextWindowBgAlpha(0.5f);
            ImGui::Begin("##status", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove);
            ImGui::Text("Photo Library — ready");
            ImGui::Text("%.1f fps", io.Framerate);
            ImGui::End();

            ImGui::Render();

            // ── Encode render ────────────────────────────────────────────────
            id<MTLRenderCommandEncoder> enc =
                [cmdBuf renderCommandEncoderWithDescriptor:rpd];
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmdBuf, enc);
            [enc endEncoding];

            [cmdBuf presentDrawable:drawable];
            [cmdBuf commit];
        }
    }

    // ── Cleanup ──────────────────────────────────────────────────────────────
    spdlog::info("Shutting down");
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
