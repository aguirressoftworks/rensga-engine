// F6.09 (B2): LineRenderer. A polyline of up to 16 points drawn as camera-facing quads (one per segment), `width` world units wide.
#include "rensga/core/gameplay.h"
#include "rensga/core/render_view.h"
#include "rensga/core/world.h"

namespace rensga {
namespace {
u32 g_lineQuads = 0;
constexpr u32 kChunk = 64;
Quad3D g_buf[kChunk];
}  // namespace

namespace Ribbons {

void segmentQuad(const CameraView& v, const Vector3& a, const Vector3& b, Scalar width, Color32 color, Quad3D& out) {
  const Vector3 dir = b - a;
  const Vector3 mid = (a + b) * 0.5f;
  const Vector3 viewDir = v.ortho ? v.forward : safeViewDir(v, mid);
  Vector3 side = Vector3::Cross(dir, viewDir);
  side = side.sqrMagnitude() > 1e-10f ? side.normalized() : v.right;
  side = side * (width * 0.5f);
  out.p[0] = a + side;
  out.p[1] = b + side;
  out.p[2] = b - side;
  out.p[3] = a - side;
  out.color = color;
  out.u0 = 0; out.v0 = 0; out.u1 = 1; out.v1 = 1;
}

void drawLines(World& world, IRenderer& renderer, const CameraView& view, i32 cullingMask) {
  g_lineQuads = 0;
  world.forEach<LineRenderer>([&](LineRenderer& lr) {
    GameObject* go = lr.gameObject();
    if (!go || !go->activeInHierarchy() || !lr.enabled() || !layerInMask(cullingMask, go->layer)) return;
    const u32 n = lr.positions.count;
    if (n < 2 || lr.color.a == 0 || lr.width <= 0) return;
    u32 tex = 0, texW = 0, texH = 0;
    if (GfxAssets::bound() && !lr.texture.isNull()) {
      const u32 ti = GfxAssets::textureIndex(lr.texture);
      const PackAssets& as = GfxAssets::assets();
      if (ti != kInvalidIndex && ti < as.textureCount) {
        tex = GfxAssets::handle(ti);
        texW = as.textures[ti].width;
        texH = as.textures[ti].height;
      }
    }
    const Matrix4 w = lr.useWorldSpace ? Matrix4::identity() : go->transform.localToWorld();
    const u32 segs = (lr.loop && n >= 3) ? n : n - 1;
    u32 inChunk = 0;
    for (u32 i = 0; i < segs; ++i) {
      const Vector3 a = w.transformPoint(lr.positions.items[i]), b = w.transformPoint(lr.positions.items[(i + 1) % n]);
      Scalar width = lr.width;
      const Scalar minW = viewPixelSize(view, (a + b) * 0.5f, 1.0f);
      if (width < minW) width = minW;  // never thinner than one pixel
      segmentQuad(view, a, b, width, lr.color, g_buf[inChunk++]);
      if (inChunk == kChunk) {
        submitQuads(renderer, view, tex, texW, texH, g_buf, inChunk);
        g_lineQuads += inChunk;
        inChunk = 0;
      }
    }
    if (inChunk) {
      submitQuads(renderer, view, tex, texW, texH, g_buf, inChunk);
      g_lineQuads += inChunk;
    }
  });
}

u32 quadsDrawn() { return g_lineQuads; }
void addQuads(u32 n) { g_lineQuads += n; }

}  // namespace Ribbons
}  // namespace rensga
