#include "rensga/render/gs.h"

#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <gsMisc.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <string.h>

#include "rensga/core/log.h"

namespace rensga {

static inline unsigned eeTicks() {  // EE core cycle counter (294.912 MHz)
  unsigned c;
  asm volatile("mfc0 %0, $9" : "=r"(c));
  return c;
}

// ---- fixed storage (no heap after init). One GsRenderer exists per program. ----
struct GsRenderer::Tex {
  u32 width = 0, height = 0;
  u8 format = 0;
  const void* pixels = nullptr;
  GSTEXTURE gs;
  u32 lastUse = 0;     // LRU clock value of the last draw
  u32 epoch = 0;       // kickEpoch_ at the last draw: equal means queued draws still read this texture's VRAM
  u16 firstPage = 0;
  u16 pages = 0;       // 0 = not resident
  u8 clutSlot = 0xFF;  // CLUT area slot while resident (CLUT4/CLUT8 only)
};

namespace {
constexpr u32 kVramBytes = 4u * 1024 * 1024;
alignas(64) ::u32 g_clut[GsRenderer::kMaxTextures][256];
alignas(64) ::u32 g_staging[256 * 256];  // RGBA8 upload copy (alpha 0..255 -> 0..128)
constexpr u32 kPageBytes = 8192;           // one GS page
constexpr u32 kClutSlotBytes = 1024;       // 16x16 CT32 = 4 blocks
u16 g_pageOwner[GsRenderer::kMaxPages];    // texture index + 1 per page, 0 = free
u16 g_clutOwner[GsRenderer::kClutSlots];   // texture index + 1 per CLUT slot, 0 = free
}  // namespace

static GsRenderer::Tex* g_texStore = nullptr;
alignas(64) static unsigned char g_texStoreBytes[sizeof(GsRenderer::Tex) * GsRenderer::kMaxTextures];
alignas(64) static GsRenderer::Command g_cmds[GsRenderer::kMaxCommands];

static inline u8 gsAlpha(u32 a) { return static_cast<u8>((a + 1) >> 1); }          // 0..255 -> 0..128
static inline u8 gsColor(u32 c) { return static_cast<u8>((c + 1) >> 1); }          // modulate: 128 = 1.0
static inline u64 gsRgbaq(u32 rgba) {
  return GS_SETREG_RGBAQ(gsColor(rgba & 0xFF), gsColor((rgba >> 8) & 0xFF), gsColor((rgba >> 16) & 0xFF), gsAlpha(rgba >> 24), 0x00);
}

// Untextured primitives are not modulated: colors are used as they are (only alpha maps 0..255 -> 0..128).
static inline u64 gsRgbaqFlat(u32 rgba) {
  return GS_SETREG_RGBAQ(rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF, gsAlpha(rgba >> 24), 0x00);
}

static int cmpOrder(const void* a, const void* b) {
  u64 x = static_cast<const GsRenderer::Command*>(a)->order, y = static_cast<const GsRenderer::Command*>(b)->order;
  return x < y ? -1 : (x > y ? 1 : 0);
}

bool GsRenderer::init(u32, u32) {
  g_texStore = reinterpret_cast<Tex*>(g_texStoreBytes);
  for (u32 i = 0; i < kMaxTextures; ++i) new (&g_texStore[i]) Tex();
  GSGLOBAL* g = gsKit_init_global();
  if (!g) return false;
  const bool detectedPal = g->Mode == GS_MODE_PAL;
  if (videoSet_) {
    g->Mode = video_.pal ? GS_MODE_PAL : GS_MODE_NTSC;
    switch (video_.interlace) {
      case GsInterlace::Field: g->Interlace = GS_INTERLACED; g->Field = GS_FIELD; g->Height = video_.pal ? 512 : 448; break;
      case GsInterlace::Frame: g->Interlace = GS_INTERLACED; g->Field = GS_FRAME; g->Height = video_.pal ? 512 : 448; break;
      case GsInterlace::Progressive: g->Interlace = GS_NONINTERLACED; g->Field = GS_FRAME; g->Height = video_.pal ? 256 : 224; break;
    }
  } else {
    video_.pal = detectedPal;
    video_.interlace = g->Interlace == GS_NONINTERLACED ? GsInterlace::Progressive : (g->Field == GS_FRAME ? GsInterlace::Frame : GsInterlace::Field);
  }
  g->Width = 640;
  g->PSM = GS_PSM_CT32;
  g->ZBuffering = zbuffer_ ? GS_SETTING_ON : GS_SETTING_OFF;
  g->PSMZ = GS_PSMZ_16S;
  g->DoubleBuffering = GS_SETTING_ON;
  g->PrimAlphaEnable = GS_SETTING_ON;
  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);
  gsKit_init_screen(g);
  gsKit_mode_switch(g, GS_ONESHOT);
  gs_ = g;
  // VRAM layout: gsKit placed the frame buffers (and the z buffer if on); the rest is ours.
  fbBytes_ = static_cast<u32>(g->CurrentPointer);
  clutBase_ = (fbBytes_ + kPageBytes - 1) / kPageBytes * kPageBytes;
  pageBase_ = clutBase_ + kClutSlots * kClutSlotBytes;
  pageCount_ = (kVramBytes - pageBase_) / kPageBytes;
  if (pageCount_ > kMaxPages) pageCount_ = kMaxPages;
  memset(g_pageOwner, 0, sizeof(g_pageOwner));
  memset(g_clutOwner, 0, sizeof(g_clutOwner));
  w_ = static_cast<u32>(g->Width);
  h_ = static_cast<u32>(g->Height);
  vw_ = w_;
  vh_ = h_;
  RLOG_INFO("gs", "renderer ready %ux%u %s %s, vram fb+z=%u KB clut=%u KB texture area=%u KB", w_, h_, video_.pal ? "PAL" : "NTSC",
            video_.interlace == GsInterlace::Field ? "interlaced-field" : (video_.interlace == GsInterlace::Frame ? "interlaced-frame" : "progressive"),
            fbBytes_ / 1024, kClutSlots * kClutSlotBytes / 1024, pageCount_ * kPageBytes / 1024);
  return true;
}

void GsRenderer::beginFrame(Color32 c) {
  GSGLOBAL* g = static_cast<GSGLOBAL*>(gs_);
  cmdCount_ = 0;
  frame_ = GsStats();
  frameStartTicks_ = eeTicks();
  gsKit_set_primalpha(g, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);  // (Cs - Cd) * As + Cd
  if (!zbuffer_) gsKit_set_test(g, GS_ZTEST_OFF);
  gsKit_clear(g, GS_SETREG_RGBAQ(c.r, c.g, c.b, 0x80, 0x00));
}

bool GsRenderer::push(const Command& c) {
  if (cmdCount_ >= kMaxCommands) {
    ++frame_.dropped;
    return false;
  }
  g_cmds[cmdCount_] = c;
  g_cmds[cmdCount_].order = (c.order & 0xFFFFFFFF00000000ull) | (static_cast<u64>(c.tex & 0xFFFF) << 16) | (cmdCount_ & 0xFFFF);
  ++cmdCount_;
  return true;
}

void GsRenderer::drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) {
  drawRect(Rect(static_cast<Scalar>(x), static_cast<Scalar>(y), static_cast<Scalar>(w), static_cast<Scalar>(h)), color, 0xFFFFFFFFu);
}

void GsRenderer::drawRect(const Rect& dst, Color32 color, u32 sortKey) {
  Command c;
  c.order = static_cast<u64>(sortKey) << 32;
  c.sx = c.sy = c.sw = c.sh = 0;
  c.dx = dst.x; c.dy = dst.y; c.dw = dst.width; c.dh = dst.height;
  c.color = color.r | (color.g << 8) | (color.b << 16) | (static_cast<u32>(color.a) << 24);
  c.tex = 0;
  push(c);
}

void GsRenderer::drawSprite(u32 tex, const Rect& src, const Rect& dst, Color32 tint, u32 sortKey) {
  if (tex == 0 || tex > texCount_) return;
  Command c;
  c.order = static_cast<u64>(sortKey) << 32;
  c.sx = src.x; c.sy = src.y; c.sw = src.width; c.sh = src.height;
  c.dx = dst.x; c.dy = dst.y; c.dw = dst.width; c.dh = dst.height;
  c.color = tint.r | (tint.g << 8) | (tint.b << 16) | (static_cast<u32>(tint.a) << 24);
  c.tex = tex;
  push(c);
}

void GsRenderer::drawText(u32 fontTex, const PackFontRec& f, i32 x, i32 y, const char* text, Color32 color, u32 sortKey) {
  if (!text || fontTex == 0 || fontTex > texCount_ || f.columns == 0) return;
  i32 cx = x;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p == '\n') { cx = x; y += f.lineHeight; continue; }
    i32 idx = static_cast<i32>(*p) - static_cast<i32>(f.firstChar);
    if (idx < 0 || idx >= static_cast<i32>(f.charCount) || idx >= static_cast<i32>(kPackFontMaxChars)) { cx += f.glyphW; continue; }
    const Rect src(static_cast<Scalar>((idx % f.columns) * f.glyphW), static_cast<Scalar>((idx / f.columns) * f.glyphH),
                   static_cast<Scalar>(f.glyphW), static_cast<Scalar>(f.glyphH));
    drawSprite(fontTex, src, Rect(static_cast<Scalar>(cx), static_cast<Scalar>(y), static_cast<Scalar>(f.glyphW), static_cast<Scalar>(f.glyphH)), color, sortKey);
    ++frame_.glyphs;
    cx += f.advance[idx];
  }
}

u32 GsRenderer::uploadTexture(const TextureDesc& d) {
  if (texCount_ >= kMaxTextures || !d.pixels || d.width == 0 || d.height == 0 || d.width > 1024 || d.height > 1024) {
    RLOG_ERROR("gs", "uploadTexture: bad size %ux%u or no pixels or texture table full (%u)", d.width, d.height, texCount_);
    return 0;
  }
  const bool clut = d.format != tex_format::RGBA8;
  if (d.format > tex_format::CLUT8 || (clut && !d.palette) || (!clut && (d.width > 256 || d.height > 256))) {
    RLOG_ERROR("gs", "uploadTexture: unsupported format %u (RGBA8 max 256x256, CLUT needs a palette)", d.format);
    return 0;
  }
  if (d.format == tex_format::CLUT4 && (d.width & 1)) {
    RLOG_ERROR("gs", "uploadTexture: CLUT4 width must be even (%u)", d.width);
    return 0;
  }
  Tex& t = g_texStore[texCount_];
  t = Tex();
  t.width = d.width; t.height = d.height; t.format = d.format; t.pixels = d.pixels;
  GSTEXTURE& g = t.gs;
  memset(&g, 0, sizeof(g));
  g.Width = d.width; g.Height = d.height;
  g.Filter = GS_FILTER_NEAREST;
  g.Delayed = 0;
  g.Mem = const_cast<::u32*>(static_cast<const ::u32*>(d.pixels));
  if (d.format == tex_format::RGBA8) {
    g.PSM = GS_PSM_CT32;
  } else {
    g.PSM = d.format == tex_format::CLUT8 ? GS_PSM_T8 : GS_PSM_T4;
    g.ClutPSM = GS_PSM_CT32;
    g.Clut = g_clut[texCount_];
    g.ClutStorageMode = GS_CLUT_STOREMODE_LOAD;
    const u32 n = d.format == tex_format::CLUT8 ? 256 : 16;
    for (u32 i = 0; i < n; ++i) {
      u32 e = d.palette[i];
      e = (e & 0x00FFFFFFu) | (static_cast<u32>(gsAlpha(e >> 24)) << 24);
      // CLUT8 in CSM1 stores entries in blocks of 32 with the middle two groups of 8 swapped.
      const u32 slot = n == 256 ? ((i & 0xE7u) | ((i & 0x10u) >> 1) | ((i & 0x08u) << 1)) : i;
      g_clut[texCount_][slot] = e;
    }
  }
  gsKit_setup_tbw(&g);
  if (clut && (g.TBW & 1)) ++g.TBW;  // 8/4-bit textures address in whole 128-pixel pages
  ++texCount_;
  return texCount_;
}

void GsRenderer::kick() {
  gsKit_queue_exec(static_cast<GSGLOBAL*>(gs_));  // queued draws run now, so nothing reads old VRAM contents afterwards
  ++kickEpoch_;
  ++frame_.kicks;
}

void GsRenderer::evict(u32 index) {
  Tex& v = g_texStore[index];
  for (u32 p = 0; p < v.pages; ++p) g_pageOwner[v.firstPage + p] = 0;
  if (v.clutSlot != 0xFF) g_clutOwner[v.clutSlot] = 0;
  v.pages = 0;
  v.clutSlot = 0xFF;
  ++frame_.evictions;
  ++evictionsTotal_;
}

// Picks the least recently used resident texture (with a CLUT slot if needClut) and evicts it. A victim whose draws are
// still queued forces a kick first. Returns false when nothing can be evicted.
static u32 lruVictim(const GsRenderer::Tex* store, u32 count, bool needClut) {
  u32 best = 0xFFFFFFFFu;
  for (u32 i = 0; i < count; ++i) {
    const GsRenderer::Tex& t = store[i];
    if (!t.pages || (needClut && t.clutSlot == 0xFF)) continue;
    if (best == 0xFFFFFFFFu || t.lastUse < store[best].lastUse) best = i;
  }
  return best;
}

bool GsRenderer::allocPages(Tex& t, u32 pages, u32 index) {
  (void)index;
  // CLUT slot first (CLUT textures only), evicting LRU CLUT holders when all slots are taken.
  if (t.format != tex_format::RGBA8) {
    for (;;) {
      u32 slot = kClutSlots;
      for (u32 i = 0; i < kClutSlots; ++i)
        if (!g_clutOwner[i]) { slot = i; break; }
      if (slot < kClutSlots) { t.clutSlot = static_cast<u8>(slot); g_clutOwner[slot] = static_cast<u16>(index + 1); break; }
      const u32 v = lruVictim(g_texStore, texCount_, true);
      if (v == 0xFFFFFFFFu) return false;
      if (g_texStore[v].epoch == kickEpoch_) kick();
      evict(v);
    }
  }
  for (;;) {
    u32 run = 0, first = 0;
    for (u32 p = 0; p < pageCount_; ++p) {
      if (g_pageOwner[p]) { run = 0; continue; }
      if (run == 0) first = p;
      if (++run == pages) {
        for (u32 q = 0; q < pages; ++q) g_pageOwner[first + q] = static_cast<u16>(index + 1);
        t.firstPage = static_cast<u16>(first);
        t.pages = static_cast<u16>(pages);
        return true;
      }
    }
    const u32 v = lruVictim(g_texStore, texCount_, false);
    if (v == 0xFFFFFFFFu) break;
    if (g_texStore[v].epoch == kickEpoch_) kick();
    evict(v);
  }
  if (t.clutSlot != 0xFF) { g_clutOwner[t.clutSlot] = 0; t.clutSlot = 0xFF; }
  return false;
}

bool GsRenderer::ensureResident(Tex& t) {
  t.lastUse = ++useClock_;
  t.epoch = kickEpoch_;
  if (t.pages) return true;
  GSGLOBAL* g = static_cast<GSGLOBAL*>(gs_);
  GSTEXTURE& gt = t.gs;
  const u32 index = static_cast<u32>(&t - g_texStore);
  const u32 texBytes = gsKit_texture_size(static_cast<int>(t.width), static_cast<int>(t.height), gt.PSM);
  const u32 pages = (texBytes + kPageBytes - 1) / kPageBytes;
  if (pages > pageCount_ || !allocPages(t, pages, index)) return false;  // larger than the whole texture area
  gt.Vram = pageBase_ + t.firstPage * kPageBytes;
  if (t.format != tex_format::RGBA8) {
    gt.VramClut = clutBase_ + t.clutSlot * kClutSlotBytes;
  } else {
    const u32* src = static_cast<const u32*>(t.pixels);
    const u32 n = t.width * t.height;
    for (u32 i = 0; i < n; ++i) g_staging[i] = (src[i] & 0x00FFFFFFu) | (static_cast<u32>(gsAlpha(src[i] >> 24)) << 24);
    gt.Mem = g_staging;
  }
  FlushCache(0);  // the DMA reads EE memory directly: write back what the CPU just wrote (palettes, staging, pack data)
  gsKit_texture_upload(g, &gt);
  ++frame_.uploads;
  ++uploadsTotal_;
  t.epoch = kickEpoch_;  // a kick during allocation moved the epoch on; the draw about to be queued belongs to the new one
  return true;
}

GsRenderer::VramInfo GsRenderer::vramInfo() const {
  u32 used = 0, clut = 0;
  for (u32 p = 0; p < pageCount_; ++p) used += g_pageOwner[p] ? kPageBytes : 0;
  for (u32 i = 0; i < kClutSlots; ++i) clut += g_clutOwner[i] ? kClutSlotBytes : 0;
  return VramInfo{fbBytes_, pageCount_ * kPageBytes, used, clut, pageCount_ * kPageBytes - used, evictionsTotal_, uploadsTotal_};
}

void GsRenderer::logVram() const {
  const VramInfo v = vramInfo();
  RLOG_INFO("gs", "rensga: vram fb=%uKB tex=%uKB used/%uKB area clut=%uKB free=%uKB evictions=%u uploads=%u", v.fbBytes / 1024,
            v.usedBytes / 1024, v.areaBytes / 1024, v.clutBytes / 1024, v.freeBytes / 1024, v.evictions, v.uploads);
}

void GsRenderer::endFrame() {
  GSGLOBAL* g = static_cast<GSGLOBAL*>(gs_);
  bool sorted = true;
  for (u32 i = 1; i < cmdCount_; ++i)
    if (g_cmds[i].order < g_cmds[i - 1].order) { sorted = false; break; }
  if (!sorted) qsort(g_cmds, cmdCount_, sizeof(Command), cmpOrder);
  const float sx = static_cast<float>(w_) / static_cast<float>(vw_), sy = static_cast<float>(h_) / static_cast<float>(vh_);
  for (u32 i = 0; i < cmdCount_; ++i) {
    const Command& c = g_cmds[i];
    const float x1 = c.dx * sx, y1 = c.dy * sy, x2 = (c.dx + c.dw) * sx, y2 = (c.dy + c.dh) * sy;
    if (c.tex == 0) {
      gsKit_prim_sprite(g, x1, y1, x2, y2, 0, gsRgbaqFlat(c.color));
      ++frame_.rects;
    } else {
      Tex& t = g_texStore[c.tex - 1];
      if (!ensureResident(t)) { ++frame_.dropped; continue; }
      gsKit_prim_sprite_texture_3d(g, &t.gs, x1, y1, 0, c.sx, c.sy, x2, y2, 0, c.sx + c.sw, c.sy + c.sh, gsRgbaq(c.color));
      ++frame_.sprites;
    }
  }
  gsKit_queue_exec(g);
  frame_.cpuMicros = static_cast<u32>((eeTicks() - frameStartTicks_) / 295u);
  gsKit_sync_flip(g);
  const VramInfo vi = vramInfo();
  frame_.vramUsed = clutBase_ + kClutSlots * kClutSlotBytes + vi.usedBytes;
  stats_ = frame_;
  if (++frameNo_ % kLogEveryFrames == 0) logVram();
}

void GsRenderer::dumpFrameBuffers(const char* base) {
  GSGLOBAL* g = static_cast<GSGLOBAL*>(gs_);
  char path[96];
  for (int i = 0; i < 2; ++i) {
    snprintf(path, sizeof(path), "%s%d.raw", base, i);
    gsKit_vram_dump(g, path, g->ScreenBuffer[i], g->ScreenBuffer[i] + w_ * h_ * 4);
    RLOG_INFO("gs", "dumped %s (%ux%u)", path, w_, h_);
  }
}

}  // namespace rensga
