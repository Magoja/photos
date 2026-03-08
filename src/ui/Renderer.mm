// Renderer.mm — Metal + ImGui frame management
#include "Renderer.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_metal.h"
#include <spdlog/spdlog.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <chrono>

namespace ui {

struct Renderer::Impl {
    id<MTLDevice>             device;
    id<MTLCommandQueue>       cmdQueue;
    CAMetalLayer*             layer;
    MTLRenderPassDescriptor*  rpd;
    id<CAMetalDrawable>       currentDrawable;
    id<MTLCommandBuffer>      currentCmdBuf;
    std::chrono::steady_clock::time_point lastFrameTime;
};

static CAMetalLayer* setupMetalLayer(SDL_Window* window, id<MTLDevice> device)
{
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    NSWindow* nsWin = wmInfo.info.cocoa.window;
    NSView*   view  = nsWin.contentView;
    view.wantsLayer = YES;

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device        = device;
    layer.pixelFormat   = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    view.layer          = layer;
    return layer;
}

bool Renderer::init(SDL_Window* window) {
    impl_ = new Impl{};

    impl_->device   = MTLCreateSystemDefaultDevice();
    if (!impl_->device) { spdlog::error("No Metal device"); return false; }

    impl_->layer    = setupMetalLayer(window, impl_->device);
    impl_->cmdQueue = [impl_->device newCommandQueue];

    impl_->rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    impl_->rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    impl_->rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    impl_->rpd.colorAttachments[0].clearColor  =
        MTLClearColorMake(0.12, 0.12, 0.12, 1.0);

    // ImGui already initialized in main; just set Metal-specific state
    ImGui_ImplMetal_Init(impl_->device);

    texMgr_.init((MTLDevicePtr)impl_->device);

    impl_->lastFrameTime = std::chrono::steady_clock::now();
    spdlog::info("Renderer initialized");
    return true;
}

void Renderer::shutdown() {
    if (!impl_) return;
    ImGui_ImplMetal_Shutdown();
    delete impl_;
    impl_ = nullptr;
}

Renderer::~Renderer() { shutdown(); }

void Renderer::beginFrame() {
    if (!impl_) return;

    // Resize layer to current display size
    ImGuiIO& io = ImGui::GetIO();
    impl_->layer.drawableSize = CGSizeMake(
        (CGFloat)io.DisplaySize.x * io.DisplayFramebufferScale.x,
        (CGFloat)io.DisplaySize.y * io.DisplayFramebufferScale.y);

    impl_->currentDrawable = [impl_->layer nextDrawable];
    if (!impl_->currentDrawable) return;
    impl_->rpd.colorAttachments[0].texture = impl_->currentDrawable.texture;

    impl_->currentCmdBuf = [impl_->cmdQueue commandBuffer];

    ImGui_ImplMetal_NewFrame(impl_->rpd);
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void Renderer::endFrame() {
    if (!impl_ || !impl_->currentDrawable) return;

    ImGui::Render();

    id<MTLRenderCommandEncoder> enc =
        [impl_->currentCmdBuf renderCommandEncoderWithDescriptor:impl_->rpd];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
                                    impl_->currentCmdBuf, enc);
    [enc endEncoding];
    [impl_->currentCmdBuf presentDrawable:impl_->currentDrawable];
    [impl_->currentCmdBuf commit];

    auto now = std::chrono::steady_clock::now();
    frameTimeMs_ = std::chrono::duration<float, std::milli>(
        now - impl_->lastFrameTime).count();
    impl_->lastFrameTime = now;
}

} // namespace ui
