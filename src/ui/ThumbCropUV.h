#pragma once

namespace ui {

struct ThumbMeta {
  bool  preCropped = false;
  float cropX = 0.f, cropY = 0.f, cropW = 1.f, cropH = 1.f;
};

struct CropUV { float u0, v0, u1, v1; };

CropUV thumbCropUV(const ThumbMeta& m);

}  // namespace ui
