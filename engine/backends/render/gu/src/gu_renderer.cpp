#include "rensga/render/gu.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <string.h>

#include "gu_internal.h"
#include "rensga/core/log.h"

namespace rensga {

namespace {
using namespace gu_vram;
inline unsigned U32(u32 v) { return static_cast<unsigned>(v); }
constexpr int kBufWidth = static_cast<int>(kStride);
constexpr int kScreenWi = static_cast<int>(kScreenW), kScreenHi = static_cast<int>(kScreenH);
// 5650 is the framebuffer format of F4; 8888 only while the frame dump of main_psp still wants it (see kFbBytesPerPixel).
constexpr int kFbPsm = kFbBytesPerPixel == 2 ? GU_PSM_5650 : GU_PSM_8888;
// Two display lists, used on alternate frames: the GE may still be reading list N&1 while the CPU records the next one.
constexpr u32 kListWords = kGuListBytes / 4;
alignas(64) unsigned int g_list[2][kListWords];

inline u8* vramUncached() { return reinterpret_cast<u8*>(reinterpret_cast<unsigned>(sceGeEdramGetAddr()) | 0x40000000u); }

void clearBuffer(u32 listIndex, u32 fbOffset, u32 color) {
  sceGuStart(GU_DIRECT, g_list[listIndex]);
  sceGuDrawBufferList(kFbPsm, reinterpret_cast<void*>(fbOffset), kBufWidth);
  sceGuClearColor(color);
  sceGuClear(GU_COLOR_BUFFER_BIT);
  sceGuFinish();
  sceGuSync(0, 0);
}
}  // namespace

bool GuRenderer::init(u32 width, u32 height) {
  w_ = width ? width : kScreenW;
  h_ = height ? height : kScreenH;
  map_ = GuViewMap::fit(w_, h_);
  guInitSpriteStorage(queue_);
  vram_.init(kTexBegin, kTexEnd);
  texCount_ = 0;
  ramTex_ = 0;
  compact_ = false;
  // Buffers in VRAM (offsets from the VRAM base): draw buffer, display buffer, 16-bit depth buffer.
  void* fbp0 = reinterpret_cast<void*>(kFb0);
  void* fbp1 = reinterpret_cast<void*>(kFb1);
  void* zbp = reinterpret_cast<void*>(kDepth);
  sceGuInit();
  sceGuStart(GU_DIRECT, g_list[0]);
  sceGuDrawBuffer(kFbPsm, fbp0, kBufWidth);
  sceGuDispBuffer(kScreenWi, kScreenHi, fbp1, kBufWidth);
  sceGuDepthBuffer(zbp, kBufWidth);
  sceGuOffset(2048 - (kScreenWi / 2), 2048 - (kScreenHi / 2));
  sceGuViewport(2048, 2048, kScreenWi, kScreenHi);
  sceGuDepthRange(65535, 0);
  sceGuScissor(0, 0, kScreenWi, kScreenHi);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDisable(GU_CULL_FACE);  // cooked winding is not guaranteed in the MVP
  sceGuShadeModel(GU_SMOOTH);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  // 4x4 ordered dither (zero-mean, -8..7). It is only enabled for the 3D pass: it lifts the banding of a 5650 buffer.
  static const ScePspIMatrix4 kDither = {{-8, 0, -6, 2}, {4, -4, 6, -2}, {-5, 3, -7, 1}, {7, -1, 5, -3}};
  sceGuSetDither(&kDither);
  sceGuFinish();
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  // Both buffers start black, so the first swap shows black and not leftover VRAM.
  clearBuffer(0, kFb0, 0xFF000000u);
  clearBuffer(1, kFb1, 0xFF000000u);
  sceGuDisplay(GU_TRUE);
  drawFb_ = kFb0;  // sceGuInit draws into fbp0; the first endFrame swaps and draws into fb1
  lastDrawnFb_ = kFb0;
  frame_ = 0;
  ready_ = true;
  RLOG_INFO("gu", "static buffers %u KiB (2 lists %u KiB, %u sprites)", static_cast<unsigned>(guStaticBytes() / 1024),
            static_cast<unsigned>(2 * kGuListBytes / 1024), static_cast<unsigned>(kMaxSprites));
  RLOG_INFO("gu", "renderer ready %ux%u fb=%s tex area %u KiB", static_cast<unsigned>(w_), static_cast<unsigned>(h_),
            kFbPsm == GU_PSM_5650 ? "5650" : "8888", static_cast<unsigned>(kTexArea / 1024));
  return true;
}

void GuRenderer::setMeshes(const MeshTable& table) {
  table_ = table;
  compact_ = table.ext && table.ext->format == kMeshFormatCompact16;
  const u32 compact = compact_ ? table.meshCount : 0;
  RLOG_INFO("gu", "meshes compact=%u float=%u", static_cast<unsigned>(compact),
            static_cast<unsigned>(table.meshCount - compact));
  // The pack was written by the CPU; make it visible to the GE.
  sceKernelDcacheWritebackAll();
}

void GuRenderer::beginFrame(Color32 clear) {
  clear_ = clear;
  drawCount_ = 0;
  queue_.clear();
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
  d.ex = false;
  d.params = DrawParams();
  // A lit or skinned mesh drawn without parameters is shown with its vertex colors only.
  if (table_.lit && !table_.lit[meshIndex].isBaked()) {
    d.ex = true;
    d.params.matFlags = kMatUnlit;
  }
}

void GuRenderer::drawMeshEx(u32 meshIndex, const Matrix4& world, u32 sortKey, const DrawParams& params) {
  if (drawCount_ >= kMaxDraws || meshIndex >= table_.meshCount) return;
  Draw& d = draws_[drawCount_++];
  d.key = sortKey;
  d.mesh = meshIndex;
  d.world = world;
  d.params = params;
  d.ex = table_.lit && !table_.lit[meshIndex].isBaked();  // a baked mesh behaves exactly like drawMesh
}

void GuRenderer::setState3D() {
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);  // 3D meshes are vertex colored; no state from the 2D pass may leak in
  sceGuDisable(GU_ALPHA_TEST);
  sceGuEnable(GU_DITHER);
  mode3D_ = 0;
  curTex_ = 0;
  lightSig_ = 0;
  for (int i = 0; i < 4; ++i) sceGuDisable(GU_LIGHT0 + i);
  hwMask_ = 0;
  cullState_ = 0xFF;
  alphaTest3D_ = false;
  if (fogOn_) {
    sceGuFog(fogStart_, fogEnd_, fogColor_.packedABGR());
    sceGuEnable(GU_FOG);
  } else {
    sceGuDisable(GU_FOG);
  }
  sceGuTexFlush();
}

void GuRenderer::drawMeshes() {
  setState3D();
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(proj_.m));
  sceGumMatrixMode(GU_VIEW);
  sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(view_.m));
  // Compact meshes: the GE reads signed 16-bit positions as q / 32768; the pack says position = offset + q * scale.
  Matrix4 unpack = Matrix4::identity();
  if (compact_) {
    const PackMeshExt& e = *table_.ext;
    unpack = Matrix4::translation(Vector3(e.offset[0], e.offset[1], e.offset[2])) *
             Matrix4::scaling(Vector3(e.scale[0] * 32768.0f, e.scale[1] * 32768.0f, e.scale[2] * 32768.0f));
  }
  const PackVertexCompact* compactVerts = reinterpret_cast<const PackVertexCompact*>(table_.vertices);
  triangles_ = 0;
  litDraws_ = batchesDrawn_ = clipped_ = litSlots_ = 0;
  for (u32 oi = 0; oi < drawCount_; ++oi) {
    const Draw& d = draws_[order_[oi]];
    if (static_cast<u32>(sceGuCheckList()) + kGuListMargin > kGuListBytes) { ++listOverflows_; break; }
    if (d.ex) {
      drawLit(d, table_.lit[d.mesh]);
      continue;
    }
    const PackMeshRec& m = table_.meshes[d.mesh];
    if (m.vertexOffset + m.vertexCount > table_.vertexCount) continue;
    enterBakedState();
    sceGumMatrixMode(GU_MODEL);
    if (compact_) {
      const Matrix4 model = d.world * unpack;
      sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(model.m));
    } else {
      sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(d.world.m));
    }
    sceGumUpdateMatrix();
    if (compact_) {
      sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D, m.vertexCount, nullptr,
                      compactVerts + m.vertexOffset);
    } else {
      sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, m.vertexCount, nullptr,
                      table_.vertices + m.vertexOffset);
    }
    triangles_ += m.vertexCount / 3;
  }
  leave3DState();
}

#ifdef RENSGA_GU_SELFTEST
void guSelfTest(GuRenderer& r, u32 frame);
#endif

void GuRenderer::endFrame() {
#ifdef RENSGA_GU_DUMP_FRAME
  // Dev/CI hook: write host0:/rensga_shot.bmp when frame N starts (frame N-1 is the last one drawn).
  if (frame_ == RENSGA_GU_DUMP_FRAME) dumpFrame();
#endif
#ifdef RENSGA_GU_DUMP_FRAMES
  {
    // Dev/CI hook (F4.07): "a,b,c" frame numbers; each one writes host0:/rensga_shot_<frame>.bmp (frame N-1 is the last one drawn).
    // Parsed from the string literal every frame (a few dozen compares, no heap); the paths are built in a stack buffer.
    const char* p = RENSGA_GU_DUMP_FRAMES;
    while (*p) {
      u32 n = 0;
      bool digits = false;
      while (*p >= '0' && *p <= '9') { n = n * 10 + static_cast<u32>(*p - '0'); ++p; digits = true; }
      if (digits && n == frame_) {
        char path[48] = "host0:/rensga_shot_";
        size_t o = 19;
        char num[12];
        int nd = 0;
        for (u32 v = n; v || !nd; v /= 10) num[nd++] = static_cast<char>('0' + v % 10);
        while (nd) path[o++] = num[--nd];
        const char ext[] = ".bmp";
        for (size_t i = 0; i < sizeof(ext); ++i) path[o++] = ext[i];
        dumpFrame(path);
      }
      while (*p && (*p < '0' || *p > '9')) ++p;
    }
  }
#endif
#ifdef RENSGA_GU_SELFTEST
  guSelfTest(*this, frame_);  // dev only: queues a 2D test panel on top of the scene (see gu_selftest.cpp)
#endif
  // Insertion sort of indices by key, front to back, stable (small N, no heap; the draws themselves stay put).
  for (u32 i = 0; i < drawCount_; ++i) order_[i] = static_cast<u16>(i);
  for (u32 i = 1; i < drawCount_; ++i) {
    const u16 t = order_[i];
    const u32 k = draws_[t].key;
    u32 j = i;
    while (j > 0 && draws_[order_[j - 1]].key > k) { order_[j] = order_[j - 1]; --j; }
    order_[j] = t;
  }

  // Frame order: sync (the previous frame's list is done), vblank, swap, start(list N&1), build, finish.
  // finish does not wait: the GE draws this frame while the CPU runs the next update.
  sceGuSync(0, 0);
  sceDisplayWaitVblankStart();
  sceGuSwapBuffers();
  drawFb_ = drawFb_ == kFb0 ? kFb1 : kFb0;
  if (uploadsDirty_) {  // one writeback for everything uploaded since the last frame (pack pixels and palettes in RAM)
    sceKernelDcacheWritebackAll();
    uploadsDirty_ = false;
    RLOG_INFO("gu", "vram tex=%u/%u ram_tex=%u", static_cast<unsigned>(vram_.used()), static_cast<unsigned>(vram_.area()),
              static_cast<unsigned>(ramTex_));
  }
  sceGuStart(GU_DIRECT, g_list[frame_ & 1]);
  sceGuDrawBufferList(kFbPsm, reinterpret_cast<void*>(drawFb_), kBufWidth);  // do not rely on the swap's own command
  sceGuDisable(GU_DITHER);  // a flat clear color must stay flat; the 3D pass turns dither on
  sceGuClearColor(clear_.packedABGR());
  sceGuClearDepth(0);
  sceGuClear(drawCount_ ? (GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT) : GU_COLOR_BUFFER_BIT);
  if (drawCount_) drawMeshes();
  drawSprites();
  lastListBytes_ = static_cast<u32>(sceGuCheckList());
  sceGuFinish();
  lastDrawnFb_ = drawFb_;
  if (litDraws_ && frame_ % 60 == 0) {
    RLOG_INFO("gu", "rensga: gu draws=%u lights=%u batches=%u clipped=%u hwlights=%u tris=%u", U32(drawCount_), U32(lightCount_),
              U32(batchesDrawn_), U32(clipped_), U32(litSlots_), U32(triangles_));
  }
  ++frame_;
}

void GuRenderer::flush() {
  if (ready_) sceGuSync(0, 0);
}

bool GuRenderer::dumpFrame(const char* path) {
  if (!ready_) return false;
  flush();
  const u8* fb = vramUncached() + lastDrawnFb_;
  const int w = kScreenWi, h = kScreenHi, rowBytes = w * 3;
  const u32 dataSize = static_cast<u32>(rowBytes * h), fileSize = 54 + dataSize;
  unsigned char hdr[54] = {'B', 'M'};
  auto put32 = [&](int o, u32 v) { for (int i = 0; i < 4; ++i) hdr[o + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff); };
  put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, static_cast<u32>(w)); put32(22, static_cast<u32>(h));
  hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);
  SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd < 0) return false;
  bool ok = sceIoWrite(fd, hdr, 54) == 54;
  unsigned char row[480 * 3];
  for (int y = h - 1; y >= 0 && ok; --y) {  // BMP rows run bottom to top
    const u8* src = fb + static_cast<u32>(y) * kStride * kFbBytesPerPixel;
    for (int x = 0; x < w; ++x) {
      unsigned char r, g, b;
      if (kFbBytesPerPixel == 2) {
        const u32 p = static_cast<u32>(src[x * 2]) | (static_cast<u32>(src[x * 2 + 1]) << 8);  // R0-4 G5-10 B11-15
        const u32 r5 = p & 31, g6 = (p >> 5) & 63, b5 = (p >> 11) & 31;
        r = static_cast<unsigned char>((r5 << 3) | (r5 >> 2));
        g = static_cast<unsigned char>((g6 << 2) | (g6 >> 4));
        b = static_cast<unsigned char>((b5 << 3) | (b5 >> 2));
      } else {
        r = src[x * 4]; g = src[x * 4 + 1]; b = src[x * 4 + 2];
      }
      row[x * 3] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r;
    }
    ok = sceIoWrite(fd, row, static_cast<u32>(rowBytes)) == rowBytes;
  }
  sceIoClose(fd);
  return ok;
}

}  // namespace rensga
