// PS2 VRAM manager stress (F2.02): 12 CLUT8 + 12 RGBA 256x256 textures (3.8 MB of texels, more than the ~1.8 MB texture
// area) are all drawn every frame in a rotating order, so the LRU eviction, CLUT slot reuse and mid-frame kicks all run.
// Each cell must show its own pattern and its index digit(s); garbage or a wrong pattern means a VRAM overlap bug.
// Enabled by RENSGA_PS2_VRAM_STRESS (cmake -DRENSGA_PS2_VRAM_STRESS=ON). Fixed static storage, no heap.
#pragma once
#include <stdio.h>

#include "rensga/core/backends.h"
#include "rensga/render/gs.h"

namespace rensga {
namespace vramstress {

constexpr u32 kClut8 = 12, kRgba = 12, kCount = kClut8 + kRgba;
alignas(16) static u8 g_idx[kClut8][256 * 256];
alignas(16) static u32 g_pal[kClut8][256];
alignas(16) static u32 g_rgba[kRgba][256 * 256];
static u32 g_tex[kCount];

inline u32 hue(u32 h, u32 v) {  // h 0..5*43, crude saturated color ramp
  const u32 seg = (h / 43) % 6, f = (h % 43) * 6;
  u32 r = 0, g = 0, b = 0;
  switch (seg) {
    case 0: r = 255; g = f; break;
    case 1: r = 255 - f; g = 255; break;
    case 2: g = 255; b = f; break;
    case 3: g = 255 - f; b = 255; break;
    case 4: b = 255; r = f; break;
    default: b = 255 - f; r = 255; break;
  }
  r = r * v / 255; g = g * v / 255; b = b * v / 255;
  return r | (g << 8) | (b << 16) | (255u << 24);
}

inline bool init(GsRenderer& r) {
  for (u32 k = 0; k < kClut8; ++k) {
    for (u32 i = 0; i < 256; ++i) g_pal[k][i] = i == 0 ? 0u : hue((i * 3 + k * 20) % 256, 255);
    for (u32 y = 0; y < 256; ++y)
      for (u32 x = 0; x < 256; ++x) {
        u32 v = ((x + y * (k % 3 + 1)) / 4) & 0xFF;  // diagonal bands, slope depends on k
        if (x < 4 || y < 4 || x > 251 || y > 251) v = 255;
        g_idx[k][y * 256 + x] = static_cast<u8>(v == 0 ? 1 : v);
      }
    TextureDesc d;
    d.width = d.height = 256; d.format = tex_format::CLUT8; d.pixels = g_idx[k]; d.palette = g_pal[k];
    g_tex[k] = r.uploadTexture(d);
  }
  for (u32 k = 0; k < kRgba; ++k) {
    const u32 base = hue(k * 21, 255) & 0x00FFFFFFu;
    for (u32 y = 0; y < 256; ++y)
      for (u32 x = 0; x < 256; ++x) {
        const u32 ring = (x > 127 ? x - 128 : 127 - x) > (y > 127 ? y - 128 : 127 - y) ? (x > 127 ? x - 128 : 127 - x) : (y > 127 ? y - 128 : 127 - y);
        const bool on = ((ring / 16) + k) & 1;
        const u32 a = (x < 32 && y < 32) ? 128 : 255;  // translucent corner checks the alpha conversion
        g_rgba[k][y * 256 + x] = (on ? base : (0x00FFFFFFu & ~base)) | (a << 24);
      }
    TextureDesc d;
    d.width = d.height = 256; d.format = tex_format::RGBA8; d.pixels = g_rgba[k];
    g_tex[kClut8 + k] = r.uploadTexture(d);
  }
  for (u32 k = 0; k < kCount; ++k)
    if (!g_tex[k]) return false;
  return true;
}

// 6x4 grid on a 640x448 target; sort keys rotate with the frame so the texture order changes every frame.
inline void draw(GsRenderer& r, u32 frame, u32 fontTex, const PackFontRec& font) {
  const Scalar W = static_cast<Scalar>(r.width()), H = static_cast<Scalar>(r.height());
  const Scalar cw = W / 6, ch = (H - 16) / 4;
  for (u32 k = 0; k < kCount; ++k) {
    const Scalar x = (k % 6) * cw, y = 16 + (k / 6) * ch;
    r.drawSprite(g_tex[k], Rect(0, 0, 256, 256), Rect(x + 2, y + 2, cw - 4, ch - 4), Color32(255, 255, 255, 255), (k + frame) % kCount);
    char label[8];
    snprintf(label, sizeof(label), "%u", k);
    r.drawText(fontTex, font, static_cast<i32>(x) + 6, static_cast<i32>(y) + 6, label, Color32(255, 255, 255, 255), 100 + k);
  }
}

}  // namespace vramstress
}  // namespace rensga
