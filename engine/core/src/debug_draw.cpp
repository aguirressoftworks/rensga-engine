// F6.09 (B2): Debug::DrawLine in dev builds. The ring of lines in debug.cpp is drawn as camera-facing quads, about 1.5 pixels wide.
#include "rensga/core/debug.h"
#include "rensga/core/render_view.h"

namespace rensga {
namespace Ribbons {

void drawDebug(IRenderer& renderer, const CameraView& view) {
  if (!debugDrawEnabled() || Debug::activeLineCount() == 0) return;
  constexpr u32 kChunk = 64;
  static Quad3D buf[kChunk];
  u32 inChunk = 0;
  for (u32 i = 0; i < Debug::lineCapacity(); ++i) {
    const DebugLine& l = Debug::lineAt(i);
    if (!l.active) continue;
    segmentQuad(view, l.a, l.b, viewPixelSize(view, (l.a + l.b) * 0.5f, 1.5f), l.color, buf[inChunk++]);
    if (inChunk == kChunk) {
      submitQuads(renderer, view, 0, 0, 0, buf, inChunk);
      addQuads(inChunk);
      inChunk = 0;
    }
  }
  if (inChunk) {
    submitQuads(renderer, view, 0, 0, 0, buf, inChunk);
    addQuads(inChunk);
  }
}

}  // namespace Ribbons
}  // namespace rensga
