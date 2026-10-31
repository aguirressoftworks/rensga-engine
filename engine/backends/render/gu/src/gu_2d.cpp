// F4.04 (agent "gu"): GuRenderer 2D path (sprites, rects and text as GU_SPRITES in through mode, static radix sort by
// (sortKey, tex, seq)), texture upload and the VRAM texture area. PSP target only.
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <string.h>

#include "rensga/core/log.h"
#include "gu_internal.h"
#include "rensga/render/gu.h"

namespace rensga {

namespace {
constexpr u32 kSpriteVertexType = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D;

// Frame-lifetime storage lives in BSS (no heap, and the renderer object stays small enough for the main thread's stack).
alignas(16) GuSprite g_items[GuRenderer::kMaxSprites];
u16 g_order[2 * GuRenderer::kMaxSprites];
alignas(64) GuVertex2D g_verts[2][2 * GuRenderer::kMaxSprites];  // double buffered like the display lists
GuRun g_runs[GuRenderer::kMaxSprites];

inline unsigned U(u32 v) { return static_cast<unsigned>(v); }
bool isPow2(u32 v) { return v && !(v & (v - 1)); }

inline u8* vramUncached() { return reinterpret_cast<u8*>(reinterpret_cast<unsigned>(sceGeEdramGetAddr()) | 0x40000000u); }
inline const u8* vramGe() { return reinterpret_cast<const u8*>(sceGeEdramGetAddr()); }
}  // namespace

void guInitSpriteStorage(GuSpriteQueue& q) { q.init(g_items, g_order, GuRenderer::kMaxSprites); }
GuVertex2D* guSpriteVertices(u32 frame) { return g_verts[frame & 1]; }
GuRun* guSpriteRuns() { return g_runs; }
u32 guStaticBytes() { return sizeof(g_items) + sizeof(g_order) + sizeof(g_verts) + sizeof(g_runs) + 2 * kGuListBytes; }

void GuRenderer::setVirtualResolution(u32 w, u32 h) {
  if (!w || !h) return;
  w_ = w;
  h_ = h;
  map_ = GuViewMap::fit(w, h);
  RLOG_INFO("gu", "2d space %ux%u scale=%d.%03d offset=%d,%d", U(w), U(h), static_cast<int>(map_.scale),
            static_cast<int>(map_.scale * 1000.0f) % 1000, static_cast<int>(map_.offX), static_cast<int>(map_.offY));
}

u32 GuRenderer::uploadTexture(const TextureDesc& d) {
  if (!ready_) return 0;
  if (!d.width || !d.height || !d.pixels) return 0;
  if (texCount_ >= kMaxTextures) {
    RLOG_ERROR("gu", "uploadTexture: all %u texture handles are used", U(kMaxTextures));
    return 0;
  }
  const u32 sw = d.storedWidth ? d.storedWidth : d.width, sh = d.storedHeight ? d.storedHeight : d.height;
  u8 psm = 0, mask = 0, blocks = 0;
  u32 minW = 8;
  switch (d.format) {
    case tex_format::RGBA8: psm = GU_PSM_8888; break;
    case tex_format::RGBA5551: psm = GU_PSM_5551; break;
    case tex_format::RGBA4444: psm = GU_PSM_4444; break;
    case tex_format::CLUT4: psm = GU_PSM_T4; mask = 0x0F; blocks = 2; minW = 32; break;
    case tex_format::CLUT8: psm = GU_PSM_T8; mask = 0xFF; blocks = 32; minW = 16; break;
    default:
      RLOG_ERROR("gu", "uploadTexture: unknown format %u", static_cast<unsigned>(d.format));
      return 0;
  }
  if (d.format >= tex_format::CLUT4 && d.format <= tex_format::CLUT8 && !d.palette) {
    RLOG_ERROR("gu", "uploadTexture: CLUT texture without palette");
    return 0;
  }
  if (!isPow2(sw) || !isPow2(sh) || sw > 512 || sh > 512 || sw < minW || sh < 8) {
    RLOG_ERROR("gu", "uploadTexture: stored size %ux%u (format %u) must be a power of two, min %ux8, max 512", U(sw),
               U(sh), static_cast<unsigned>(d.format), U(minW));
    return 0;
  }
  if (d.flags & ~static_cast<u32>(tex_flag::kSwizzled)) {
    RLOG_ERROR("gu", "uploadTexture: unknown flags 0x%x", static_cast<unsigned>(d.flags));
    return 0;
  }

  Tex t;
  t.w = static_cast<u16>(sw);
  t.h = static_cast<u16>(sh);
  t.psm = psm;
  t.clutMask = mask;
  t.clutBlocks = blocks;
  t.swizzled = (d.flags & tex_flag::kSwizzled) ? 1 : 0;

  // Pixels: the first free range in the texture area, else sampled straight from the pack in RAM (needs 16-byte
  // alignment; the dcache is written back once before the next frame).
  const u32 bytes = guTextureBytes(d.format, sw, sh);
  u32 off = 0;
  if (vram_.alloc(bytes, &off)) {
    memcpy(vramUncached() + off, d.pixels, bytes);
    t.pixels = vramGe() + off;
    t.vramOff = off;
    t.vramBytes = bytes;
  } else if ((reinterpret_cast<unsigned>(d.pixels) & 15u) == 0) {
    t.pixels = d.pixels;
    ++ramTex_;
    RLOG_WARN("gu", "texture %ux%u (%u bytes) does not fit VRAM (%u free, largest block %u): sampled from RAM", U(sw),
              U(sh), U(bytes), U(vram_.area() - vram_.used()), U(vram_.largestFree()));
  } else {
    RLOG_ERROR("gu", "uploadTexture: no VRAM for %u bytes and the pixels are not 16-byte aligned", U(bytes));
    return 0;
  }
  // Palette: RGBA8 entries (16 or 256), VRAM first, else the pack's own copy.
  if (blocks) {
    const u32 entries = d.format == tex_format::CLUT4 ? 16u : 256u;
    const u32 pbytes = entries * 4;
    u32 poff = 0;
    if (vram_.alloc(pbytes, &poff)) {
      memcpy(vramUncached() + poff, d.palette, pbytes);
      t.clut = vramGe() + poff;
      t.clutVramOff = poff;
      t.clutVramBytes = pbytes;
    } else if ((reinterpret_cast<unsigned>(d.palette) & 15u) == 0) {
      t.clut = d.palette;
    } else {
      if (t.vramBytes) vram_.free(t.vramOff, t.vramBytes);
      else --ramTex_;
      RLOG_ERROR("gu", "uploadTexture: no VRAM for the palette and the palette is not 16-byte aligned");
      return 0;
    }
  }
  tex_[texCount_] = t;
  uploadsDirty_ = true;
  return ++texCount_;
}

void GuRenderer::releaseTextures() {
  if (ready_) sceGuSync(0, 0);  // the GE may still be reading them
  texCount_ = 0;
  ramTex_ = 0;
  vram_.reset();
  uploadsDirty_ = true;
}

void GuRenderer::drawSprite(u32 tex, const Rect& src, const Rect& dst, Color32 tint, u32 sortKey) {
  if (tex == 0 || tex > texCount_) return;
  queue_.add(tex, src.x, src.y, src.x + src.width, src.y + src.height, dst.x, dst.y, dst.x + dst.width,
             dst.y + dst.height, tint.packedABGR(), sortKey, map_);
}

void GuRenderer::drawRect(const Rect& dst, Color32 color, u32 sortKey) {
  queue_.add(0, 0, 0, 0, 0, dst.x, dst.y, dst.x + dst.width, dst.y + dst.height, color.packedABGR(), sortKey, map_);
}

void GuRenderer::drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) {
  // Same rule as the GL renderer: overlays sit on top of every sprite.
  drawRect(Rect(static_cast<Scalar>(x), static_cast<Scalar>(y), static_cast<Scalar>(w), static_cast<Scalar>(h)), color,
           0xFFFFFF00u);
}

void GuRenderer::drawText(u32 fontTex, const PackFontRec& f, i32 x, i32 y, const char* text, Color32 color,
                          u32 sortKey) {
  if (!text || fontTex == 0 || fontTex > texCount_ || !f.glyphW || !f.glyphH || !f.columns) return;
  const u32 count = f.charCount < kPackFontMaxChars ? f.charCount : kPackFontMaxChars;
  const u32 packed = color.packedABGR();
  i32 cx = x, cy = y;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p == '\n') {
      cx = x;
      cy += f.lineHeight ? f.lineHeight : f.glyphH;
      continue;
    }
    if (*p < f.firstChar || static_cast<u32>(*p - f.firstChar) >= count) continue;
    const u32 idx = static_cast<u32>(*p - f.firstChar);
    const float sx = static_cast<float>((idx % f.columns) * f.glyphW), sy = static_cast<float>((idx / f.columns) * f.glyphH);
    queue_.add(fontTex, sx, sy, sx + f.glyphW, sy + f.glyphH, static_cast<float>(cx), static_cast<float>(cy),
               static_cast<float>(cx + f.glyphW), static_cast<float>(cy + f.glyphH), packed, sortKey, map_);
    cx += f.advance[idx];
  }
}

void GuRenderer::bindTexture(const Tex& t) {
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(t.psm, 0, 0, t.swizzled);
  if (t.clut) {
    sceGuClutMode(GU_PSM_8888, 0, t.clutMask, 0);
    sceGuClutLoad(t.clutBlocks, t.clut);
  }
  sceGuTexImage(0, t.w, t.h, t.w, t.pixels);
}

void GuRenderer::setState2D() {
  sceGuDisable(GU_DEPTH_TEST);
  sceGuDisable(GU_DITHER);  // dither is for 3D only: it would speckle flat 2D colors
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuEnable(GU_ALPHA_TEST);
  sceGuAlphaFunc(GU_GREATER, 0, 0xFF);  // fully transparent texels are not drawn at all
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexScale(1.0f, 1.0f);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuTexFlush();
}

void GuRenderer::drawSprites() {
  lastSprites_ = queue_.count();
  if (!lastSprites_) return;
  GuVertex2D* verts = guSpriteVertices(frame_);
  const u32 runCount = queue_.build(verts, guSpriteRuns());
  sceKernelDcacheWritebackRange(verts, lastSprites_ * 2 * sizeof(GuVertex2D));
  setState2D();
  const GuRun* runs = guSpriteRuns();
  for (u32 i = 0; i < runCount; ++i) {
    const GuRun& r = runs[i];
    if (r.tex == 0) sceGuDisable(GU_TEXTURE_2D);
    else bindTexture(tex_[r.tex - 1]);
    sceGuDrawArray(GU_SPRITES, kSpriteVertexType, r.count, nullptr, verts + r.first);
    if (static_cast<u32>(sceGuCheckList()) + kGuListMargin > kGuListBytes) {  // list nearly full: drop the rest
      ++listOverflows_;
      break;
    }
  }
  sceGuDisable(GU_TEXTURE_2D);
}

}  // namespace rensga
