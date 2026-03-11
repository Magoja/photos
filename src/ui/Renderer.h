#pragma once
#include "TextureManager.h"
#include <functional>

struct SDL_Window;

namespace ui {

class Renderer {
 public:
  ~Renderer();

  bool init(SDL_Window* window);
  void shutdown();

  // Begin a new frame; call between beginFrame() and endFrame()
  void beginFrame();
  void endFrame();

  TextureManager& textures() { return texMgr_; }

  // Frame time in milliseconds
  float frameTimeMs() const { return frameTimeMs_; }

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  TextureManager texMgr_;
  float frameTimeMs_ = 0.f;
};

}  // namespace ui
