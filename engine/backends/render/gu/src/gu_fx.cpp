// F6.09 (B2): GuRenderer cameras and effects. The draws of a frame are recorded and run at endFrame, so a camera change between
// draws is a new "pass": its own view / projection, viewport rectangle (GE offset, viewport and scissor) and clear. World-space quads
// (particles, line ribbons) are recorded as triangle lists in a per-frame buffer and drawn after the meshes of their pass.
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <string.h>

#include "gu_internal.h"
#include "rensga/core/log.h"
#include "rensga/render/gu.h"

namespace rensga {

namespace {
constexpr int kScreenWi = 480, kScreenHi = 272;
constexpr u32 kQuadBytesMax = 1024 * 6 * 24;  // 1024 textured quads per frame
alignas(64) u8 g_quadBytes[2][kQuadBytesMax];
}  // namespace

// The pass new commands go to: the current one while it has no draws, else a copy of it that starts after the draws so far.
GuRenderer::Pass& GuRenderer::editPass() {
  Pass& cur = passes_[passCount_ - 1];
  const bool dirty = drawCount_ > cur.firstDraw || qrunCount_ > cur.firstRun;
  if (!dirty || passCount_ >= kMaxPasses) return cur;
  passes_[passCount_] = cur;
  Pass& n = passes_[passCount_++];
  n.firstDraw = drawCount_;
  n.firstRun = qrunCount_;
  n.clearFlags = 0;
  return n;
}

void GuRenderer::setCamera(const Matrix4& view, const Matrix4& proj) {
  Pass& p = editPass();
  p.view = view;
  p.proj = proj;
  view_ = view;
  proj_ = proj;
}

void GuRenderer::setViewport(i32 x, i32 y, i32 w, i32 h) {
  if (w <= 0 || h <= 0) return;
  Pass& p = editPass();
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > kScreenWi) w = kScreenWi - x;
  if (y + h > kScreenHi) h = kScreenHi - y;
  if (w <= 0 || h <= 0) return;
  p.x = x; p.y = y; p.w = w; p.h = h;
}

void GuRenderer::clearView(u32 flags, Color32 color) {
  Pass& p = editPass();
  p.clearFlags = flags;
  p.clearColor = color;
}

void GuRenderer::applyViewport(const Pass& p) {
  // The GE offset stays at the screen centre; the viewport centre and the scissor select the rectangle.
  sceGuOffset(2048 - kScreenWi / 2, 2048 - kScreenHi / 2);
  sceGuViewport(2048 + p.x + p.w / 2 - kScreenWi / 2, 2048 + p.y + p.h / 2 - kScreenHi / 2, p.w, p.h);
  sceGuScissor(p.x, p.y, p.w, p.h);  // GU: (x, y, width, height)
}

void GuRenderer::resetViewport() {
  sceGuOffset(2048 - kScreenWi / 2, 2048 - kScreenHi / 2);
  sceGuViewport(2048, 2048, kScreenWi, kScreenHi);
  sceGuScissor(0, 0, kScreenWi, kScreenHi);
}

bool GuRenderer::drawQuads3D(u32 tex, const Quad3D* q, u32 count, u32) {
  if (!q || !count) return true;
  const bool textured = tex != 0 && tex <= texCount_;
  const u32 vsize = textured ? 24u : 16u;
  const u32 passIndex = passCount_ - 1;
  const u32 firstRun = passes_[passIndex].firstRun;
  u8* base = g_quadBytes[frame_ & 1];
  static const u8 kOrder[6] = {0, 1, 2, 0, 2, 3};
  for (u32 i = 0; i < count; ++i) {
    if (quadBytes_ + 6 * vsize > kQuadBytesMax) { quadsDropped_ += count - i; break; }
    QuadRun* r = qrunCount_ > firstRun ? &qruns_[qrunCount_ - 1] : nullptr;
    if (!r || r->tex != (textured ? tex : 0u)) {
      if (qrunCount_ >= kMaxQuadRuns) { quadsDropped_ += count - i; break; }
      r = &qruns_[qrunCount_++];
      r->tex = textured ? tex : 0u;
      r->byteOffset = quadBytes_;
      r->vertexCount = 0;
      r->pass = passIndex;
    }
    const Quad3D& a = q[i];
    const u32 color = a.color.packedABGR();
    const float uu[4] = {static_cast<float>(a.u0), static_cast<float>(a.u1), static_cast<float>(a.u1), static_cast<float>(a.u0)};
    const float vv[4] = {static_cast<float>(a.v0), static_cast<float>(a.v0), static_cast<float>(a.v1), static_cast<float>(a.v1)};
    for (u32 k = 0; k < 6; ++k) {
      const u32 c = kOrder[k];
      float* f = reinterpret_cast<float*>(base + quadBytes_);
      if (textured) {
        f[0] = uu[c];
        f[1] = vv[c];
        memcpy(f + 2, &color, 4);
        f[3] = static_cast<float>(a.p[c].x);
        f[4] = static_cast<float>(a.p[c].y);
        f[5] = static_cast<float>(a.p[c].z);
      } else {
        memcpy(f, &color, 4);
        f[1] = static_cast<float>(a.p[c].x);
        f[2] = static_cast<float>(a.p[c].y);
        f[3] = static_cast<float>(a.p[c].z);
      }
      quadBytes_ += vsize;
    }
    r->vertexCount += 6;
    ++quadsDrawn_;
  }
  return true;
}

void GuRenderer::drawPassQuads(u32 pass) {
  u32 first = passes_[pass].firstRun;
  const u32 last = pass + 1 < passCount_ ? passes_[pass + 1].firstRun : qrunCount_;
  if (first >= last) return;
  sceKernelDcacheWritebackRange(g_quadBytes[frame_ & 1], quadBytes_);  // the GE reads the buffer when the list runs
  enterBakedState();  // no lighting, no culling, no texture; the quads set what they need
  // Alpha-blended effect state, set explicitly: nothing from the mesh or 2D passes may leak in (blend func, texture function,
  // dither, vertex color source). sceGuDepthMask takes a 16-bit mask: 0xFFFF = no depth writes (GU_TRUE would only mask bit 0).
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuDisable(GU_DITHER);
  sceGuDisable(GU_FOG);
  sceGuDepthMask(0xFFFF);
  curTex_ = ~0u;  // force the texture state (function, scale) to be sent for the first run
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
  sceGumUpdateMatrix();
  const u8* base = g_quadBytes[frame_ & 1];
  for (; first < last; ++first) {
    const QuadRun& r = qruns_[first];
    if (static_cast<u32>(sceGuCheckList()) + kGuListMargin > kGuListBytes) { ++listOverflows_; break; }
    bind3DTexture(r.tex);
    const u32 type = (r.tex ? GU_TEXTURE_32BITF : 0u) | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;
    sceGumDrawArray(GU_TRIANGLES, type, r.vertexCount, nullptr, base + r.byteOffset);
    triangles_ += r.vertexCount / 3;
  }
  sceGuDepthMask(GU_FALSE);
  sceGuDisable(GU_BLEND);
  sceGuEnable(GU_DITHER);
  if (fogOn_) sceGuEnable(GU_FOG);
  curTex_ = ~0u;  // the next 3D draw rebinds (or disables) its texture
}

}  // namespace rensga
