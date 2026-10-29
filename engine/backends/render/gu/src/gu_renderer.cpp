#include "rensga/render/gu.h"

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <string.h>

#include "rensga/core/log.h"

namespace rensga {

namespace {
constexpr int kBufWidth = 512;
constexpr int kScreenW = 480, kScreenH = 272;
constexpr u32 kFrameSize = kBufWidth * kScreenH * 4;
constexpr u32 kDepthSize = kBufWidth * kScreenH * 2;
alignas(64) unsigned int g_list[262144 / 4];  // display list (static, no heap)

struct Vtx2D { u32 color; float x, y, z; };
}  // namespace

bool GuRenderer::init(u32 width, u32 height) {
  w_ = width ? width : kScreenW;
  h_ = height ? height : kScreenH;
  // VRAM layout: draw buffer, display buffer, depth buffer (offsets from VRAM base).
  void* fbp0 = reinterpret_cast<void*>(0);
  void* fbp1 = reinterpret_cast<void*>(kFrameSize);
  void* zbp = reinterpret_cast<void*>(kFrameSize * 2);
  (void)kDepthSize;
  sceGuInit();
  sceGuStart(GU_DIRECT, g_list);
  sceGuDrawBuffer(GU_PSM_8888, fbp0, kBufWidth);
  sceGuDispBuffer(kScreenW, kScreenH, fbp1, kBufWidth);
  sceGuDepthBuffer(zbp, kBufWidth);
  sceGuOffset(2048 - (kScreenW / 2), 2048 - (kScreenH / 2));
  sceGuViewport(2048, 2048, kScreenW, kScreenH);
  sceGuDepthRange(65535, 0);
  sceGuScissor(0, 0, kScreenW, kScreenH);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDisable(GU_CULL_FACE);  // cooked winding is not guaranteed in the MVP
  sceGuShadeModel(GU_SMOOTH);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuDisplay(GU_TRUE);
  RLOG_INFO("gu", "renderer ready %ux%u", w_, h_);
  return true;
}

void GuRenderer::setMeshes(const MeshTable& table) {
  table_ = table;
  // The pack was written by the CPU; make it visible to the GE.
  sceKernelDcacheWritebackAll();
}

void GuRenderer::beginFrame(Color32 clear) {
  clear_ = clear;
  drawCount_ = 0;
  overlayCount_ = 0;
  view_ = Matrix4::identity();
  proj_ = Matrix4::identity();
}

void GuRenderer::setCamera(const Matrix4& view, const Matrix4& proj) {
  view_ = view;
  proj_ = proj;
}

void GuRenderer::drawMesh(u32 meshIndex, const Matrix4& world, u32 sortKey) {
  if (drawCount_ >= kMaxDraws || meshIndex >= table_.meshCount) return;
  Draw& d = draws_[drawCount_++];
  d.key = sortKey;
  d.mesh = meshIndex;
  d.world = world;
}

void GuRenderer::drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) {
  if (overlayCount_ >= kMaxOverlays) return;
  overlays_[overlayCount_++] = Overlay{x, y, w, h, color.packedABGR()};
}

void GuRenderer::endFrame() {
  // Insertion sort by key, front to back (small N, no heap).
  for (u32 i = 1; i < drawCount_; ++i) {
    Draw t = draws_[i];
    u32 j = i;
    while (j > 0 && draws_[j - 1].key > t.key) { draws_[j] = draws_[j - 1]; --j; }
    draws_[j] = t;
  }

  sceGuStart(GU_DIRECT, g_list);
  sceGuClearColor(clear_.packedABGR());
  sceGuClearDepth(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

  sceGuEnable(GU_DEPTH_TEST);
  sceGuDisable(GU_BLEND);
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(proj_.m));
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(view_.m));
  triangles_ = 0;
  for (u32 i = 0; i < drawCount_; ++i) {
    const Draw& d = draws_[i];
    const PackMeshRec& m = table_.meshes[d.mesh];
    if (m.vertexOffset + m.vertexCount > table_.vertexCount) continue;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(d.world.m));
    sceGumUpdateMatrix();
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, m.vertexCount, nullptr,
                    table_.vertices + m.vertexOffset);
    triangles_ += m.vertexCount / 3;
  }

  if (overlayCount_) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    for (u32 i = 0; i < overlayCount_; ++i) {
      const Overlay& o = overlays_[i];
      Vtx2D* v = static_cast<Vtx2D*>(sceGuGetMemory(2 * sizeof(Vtx2D)));
      v[0] = Vtx2D{o.color, static_cast<float>(o.x), static_cast<float>(o.y), 0};
      v[1] = Vtx2D{o.color, static_cast<float>(o.x + o.w), static_cast<float>(o.y + o.h), 0};
      sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, nullptr, v);
    }
  }

  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuSwapBuffers();
}

}  // namespace rensga
