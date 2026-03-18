#include "ThumbCropUV.h"

namespace ui {

CropUV thumbCropUV(const ThumbMeta& m) {
  if (m.preCropped || (m.cropW >= 1.f && m.cropH >= 1.f)) {
    return {0.f, 0.f, 1.f, 1.f};
  }
  return {m.cropX, m.cropY, m.cropX + m.cropW, m.cropY + m.cropH};
}

}  // namespace ui
