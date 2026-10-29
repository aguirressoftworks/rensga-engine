#include "rensga/Hud.h"

#include "rensga/core/backends.h"
#include "rensga/core/loop.h"

namespace rensga {
namespace {
struct Q { int x, y, w, h; Color32 c; };
Q g_quads[Hud::kMaxQuads];
unsigned g_count = 0;
void flush(IRenderer& r) {
  for (unsigned i = 0; i < g_count; ++i) r.drawOverlayQuad(g_quads[i].x, g_quads[i].y, g_quads[i].w, g_quads[i].h, g_quads[i].c);
  g_count = 0;
}
}  // namespace

void Hud::Quad(int x, int y, int w, int h, Color32 color) {
  setOverlayHook(&flush);  // self-registering; core never depends on the api library
  if (g_count < kMaxQuads) g_quads[g_count++] = {x, y, w, h, color};
}
unsigned Hud::count() { return g_count; }
void Hud::clear() { g_count = 0; }
}  // namespace rensga
