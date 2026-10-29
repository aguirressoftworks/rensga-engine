// Minimal HUD: scripts queue screen-space quads (pixels, origin top-left) every frame; the
// renderer draws and clears them at the end of renderWorld. Capacity is fixed (no heap).
#pragma once
#include "rensga/core/math.h"

namespace rensga {
struct Hud {
  static constexpr unsigned kMaxQuads = 64;
  static void Quad(int x, int y, int w, int h, Color32 color);
  static unsigned count();
  static void clear();
};
}  // namespace rensga
