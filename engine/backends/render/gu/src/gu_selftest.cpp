// F4.04 (agent "gu"): on-target self test of the GU 2D path. Compiled only with -DRENSGA_GU_SELFTEST (CMake option
// RENSGA_GU_SELFTEST); GuRenderer::endFrame then calls guSelfTest() so the 3D scene of any project gets a 2D test panel
// on top. It uploads one texture per format (swizzled and not, padded stored sizes, CLUT4/8, a texture that fills VRAM and
// one that has to be sampled from RAM), then draws sprites, flips, tints, scaling, clipping, sort order, text and
// 1000 small sprites. Frames < 120 use the 480x272 space; later frames use a 640x448 space (letterboxed).
#ifdef RENSGA_GU_SELFTEST
#include <string.h>

#include "rensga/core/log.h"
#include "rensga/render/gu.h"

namespace rensga {

namespace {
constexpr unsigned U(u32 v) { return static_cast<unsigned>(v); }
constexpr u32 rgba(u32 r, u32 g, u32 b, u32 a) { return r | (g << 8) | (b << 16) | (a << 24); }

alignas(16) u32 g_big[512 * 512];   // 1 MiB: fills most of the VRAM texture area
alignas(16) u32 g_ram[256 * 256];   // does not fit what is left: sampled from RAM
alignas(16) u32 g_rgba8[32 * 32];
alignas(16) u16 g_5551[32 * 32], g_5551sw[32 * 32];
alignas(16) u16 g_4444[16 * 16];
alignas(16) u8 g_clut4[64 * 16 / 2], g_clut4sw[64 * 16 / 2];
alignas(16) u8 g_clut8[32 * 32], g_clut8sw[64 * 64], g_clut8swSrc[64 * 64];
alignas(16) u32 g_pal4[16], g_pal8[256], g_pal8b[256];
alignas(16) u8 g_font[128 * 32 / 2], g_fontSw[128 * 32 / 2];
alignas(16) u32 g_fontPal[16];

u32 tex_[16];
PackFontRec g_font_;

// Contract swizzle: 8-row bands top to bottom; inside a band, 16-byte columns left to right (each 16 B x 8 rows).
void swizzle(const u8* src, u8* dst, u32 rowBytes, u32 rows) {
  for (u32 by = 0; by < rows / 8; ++by)
    for (u32 bx = 0; bx < rowBytes / 16; ++bx)
      for (u32 y = 0; y < 8; ++y) {
        memcpy(dst, src + (by * 8 + y) * rowBytes + bx * 16, 16);
        dst += 16;
      }
}

void setup(GuRenderer& r) {
  // big (VRAM) and RAM textures first
  for (u32 y = 0; y < 512; ++y)
    for (u32 x = 0; x < 512; ++x) {
      const bool c = ((x >> 4) ^ (y >> 4)) & 1;
      g_big[y * 512 + x] = rgba(c ? 220 : 40, (x >> 1) & 255, (y >> 1) & 255, 255);
    }
  for (u32 y = 0; y < 256; ++y)
    for (u32 x = 0; x < 256; ++x) {
      const bool border = x < 4 || y < 4 || x >= 252 || y >= 252;
      g_ram[y * 256 + x] = border ? rgba(255, 255, 255, 255) : rgba(255, (x ^ y) & 255, 40, 255);
    }
  TextureDesc d;
  d.width = d.height = 512; d.format = tex_format::RGBA8; d.pixels = g_big;
  tex_[0] = r.uploadTexture(d);
  d.width = d.height = 256; d.pixels = g_ram;
  tex_[1] = r.uploadTexture(d);

  for (u32 y = 0; y < 32; ++y)
    for (u32 x = 0; x < 32; ++x) {
      const bool hole = (x - 16) * (x - 16) + (y - 16) * (y - 16) < 36;
      g_rgba8[y * 32 + x] = hole ? rgba(0, 0, 0, 0) : rgba(x * 8, y * 8, 128, 255);
      const u32 r5 = (x * 8) >> 3, g5 = (y * 8) >> 3, b5 = 128 >> 3;
      g_5551[y * 32 + x] = static_cast<u16>(hole ? 0 : (r5 | (g5 << 5) | (b5 << 10) | 0x8000));
    }
  d = TextureDesc();
  d.width = d.height = 32; d.format = tex_format::RGBA8; d.pixels = g_rgba8;
  tex_[2] = r.uploadTexture(d);
  swizzle(reinterpret_cast<const u8*>(g_5551), reinterpret_cast<u8*>(g_5551sw), 64, 32);
  d.format = tex_format::RGBA5551; d.pixels = g_5551sw; d.flags = tex_flag::kSwizzled;
  tex_[3] = r.uploadTexture(d);

  // 12x12 source in a 16x16 stored 4444 texture (the cooker pads right and bottom with zeros)
  for (u32 y = 0; y < 16; ++y)
    for (u32 x = 0; x < 16; ++x)
      g_4444[y * 16 + x] = (x < 12 && y < 12) ? static_cast<u16>(((x & 1) ? 0xF : 0x4) | (0x0 << 4) | ((y & 1) ? 0xF : 0x2) << 8 | 0xF000) : 0;
  d = TextureDesc();
  d.width = d.height = 12; d.storedWidth = d.storedHeight = 16; d.format = tex_format::RGBA4444; d.pixels = g_4444;
  tex_[4] = r.uploadTexture(d);

  for (u32 i = 0; i < 16; ++i) g_pal4[i] = i ? rgba(i * 16, 255 - i * 16, (i * 37) & 255, 255) : rgba(0, 0, 0, 0);
  for (u32 y = 0; y < 16; ++y)
    for (u32 x = 0; x < 64; ++x) {
      const u8 idx = static_cast<u8>(((x >> 2) + (y >> 2)) & 15);
      u8& b = g_clut4[(y * 64 + x) / 2];
      b = (x & 1) ? static_cast<u8>((b & 0x0F) | (idx << 4)) : static_cast<u8>((b & 0xF0) | idx);  // low nibble first
    }
  swizzle(g_clut4, g_clut4sw, 32, 16);
  d = TextureDesc();
  d.width = 64; d.height = 16; d.format = tex_format::CLUT4; d.pixels = g_clut4sw; d.palette = g_pal4;
  d.flags = tex_flag::kSwizzled;
  tex_[5] = r.uploadTexture(d);

  for (u32 i = 0; i < 256; ++i) { g_pal8[i] = i ? rgba(i, (i * 3) & 255, 255 - i, 255) : rgba(0, 0, 0, 0); g_pal8b[i] = rgba(255 - i, i, 128, 255); }
  for (u32 y = 0; y < 32; ++y)
    for (u32 x = 0; x < 32; ++x) g_clut8[y * 32 + x] = static_cast<u8>(((x / 4) * 8 + (y / 4)) * 4);
  d = TextureDesc();
  d.width = d.height = 32; d.format = tex_format::CLUT8; d.pixels = g_clut8; d.palette = g_pal8;
  tex_[6] = r.uploadTexture(d);
  for (u32 y = 0; y < 64; ++y)
    for (u32 x = 0; x < 64; ++x) g_clut8swSrc[y * 64 + x] = static_cast<u8>(x * 2 + (y & 1));
  swizzle(g_clut8swSrc, g_clut8sw, 64, 64);
  d.width = d.height = 64; d.pixels = g_clut8sw; d.palette = g_pal8b; d.flags = tex_flag::kSwizzled;
  tex_[7] = r.uploadTexture(d);

  // font: 16 x 4 glyphs of 8x8 in a 128x32 CLUT4 texture. Each glyph is a box with its code in dots.
  memset(g_font, 0, sizeof(g_font));
  for (u32 gi = 0; gi < 64; ++gi) {
    const u32 gx = (gi % 16) * 8, gy = (gi / 16) * 8;
    for (u32 y = 0; y < 8; ++y)
      for (u32 x = 0; x < 8; ++x) {
        const bool box = x == 0 || y == 0 || x == 7 || y == 7;
        const bool dot = x >= 1 && x <= 6 && y >= 2 && y <= 5 && (((gi >> ((y - 2) * 6 / 4 + (x - 1) / 4)) & 1) != 0) && (x & 1);
        const u8 idx = box ? 1 : (dot ? 2 : 0);
        const u32 px = gx + x, py = gy + y;
        u8& b = g_font[(py * 128 + px) / 2];
        b = (px & 1) ? static_cast<u8>((b & 0x0F) | (idx << 4)) : static_cast<u8>((b & 0xF0) | idx);
      }
  }
  g_fontPal[0] = rgba(0, 0, 0, 0); g_fontPal[1] = rgba(255, 255, 255, 255); g_fontPal[2] = rgba(255, 220, 0, 255);
  for (u32 i = 3; i < 16; ++i) g_fontPal[i] = rgba(255, 0, 255, 255);
  swizzle(g_font, g_fontSw, 64, 32);
  d = TextureDesc();
  d.width = 128; d.height = 32; d.format = tex_format::CLUT4; d.pixels = g_fontSw; d.palette = g_fontPal;
  d.flags = tex_flag::kSwizzled;
  tex_[8] = r.uploadTexture(d);
  g_font_ = PackFontRec();
  g_font_.texture = 0; g_font_.glyphW = g_font_.glyphH = 8; g_font_.firstChar = 32; g_font_.charCount = 64;
  g_font_.columns = 16; g_font_.lineHeight = 10;
  for (u32 i = 0; i < 64; ++i) g_font_.advance[i] = (i >= 33 && i < 40) ? 6 : 8;  // 'A'..'G' are narrower
  RLOG_INFO("gu", "selftest textures: %u %u %u %u %u %u %u %u %u", U(tex_[0]), U(tex_[1]), U(tex_[2]), U(tex_[3]), U(tex_[4]),
            U(tex_[5]), U(tex_[6]), U(tex_[7]), U(tex_[8]));
}
}  // namespace

void guSelfTest(GuRenderer& r, u32 frame) {
  if (frame == 0) setup(r);
  if (frame == 100 || frame == 135)
    RLOG_INFO("gu", "selftest: frame %u sprites=%u list=%u bytes dropped=%u overflows=%u", U(frame), U(r.lastSpriteCount()),
              U(r.lastListBytes()), U(r.spritesDropped()), U(r.listOverflows()));
  const Color32 white(255, 255, 255, 255);
  const bool big = frame >= 120;
  if (frame == 0 || frame == 120) r.setVirtualResolution(big ? 640 : 480, big ? 448 : 272);
  const float k = big ? 640.0f / 480.0f : 1.0f;  // lay the same picture out in the larger space
  auto P = [&](float v) { return v * k; };
  // panel with a translucent backdrop over the lower part (key 0 = behind everything below)
  r.drawRect(Rect(0, P(60), P(480), P(212)), Color32(20, 20, 40, 220), 1);
  // row 1: one sprite per format
  r.drawSprite(tex_[2], Rect(0, 0, 32, 32), Rect(P(10), P(70), 32, 32), white, 10);                  // RGBA8 1x
  r.drawSprite(tex_[2], Rect(0, 0, 32, 32), Rect(P(50) + 32, P(70), -32, 32), white, 10);            // mirrored by negative width
  r.drawSprite(tex_[2], Rect(32, 0, -32, 32), Rect(P(90), P(70), 64, 64), Color32(255, 128, 128, 255), 10);  // flipped UV + tint, 2x
  r.drawSprite(tex_[3], Rect(0, 0, 32, 32), Rect(P(170), P(70), 32, 32), white, 10);                  // 5551 swizzled
  r.drawSprite(tex_[4], Rect(0, 0, 12, 12), Rect(P(210), P(70), 12, 12), white, 10);                  // 4444, 12x12 in 16x16
  r.drawSprite(tex_[4], Rect(0, 0, 12, 12), Rect(P(230), P(70), 24, 24), white, 10);                  // 4444 2x
  r.drawSprite(tex_[5], Rect(0, 0, 64, 16), Rect(P(270), P(70), 64, 16), white, 10);                  // CLUT4 swizzled
  r.drawSprite(tex_[6], Rect(0, 0, 32, 32), Rect(P(340), P(70), 32, 32), white, 10);                  // CLUT8
  r.drawSprite(tex_[7], Rect(0, 0, 64, 64), Rect(P(380), P(70), 64, 64), white, 10);                  // CLUT8 swizzled, own palette
  // row 2: VRAM-filling texture and the RAM-sampled one, partial source rects
  r.drawSprite(tex_[0], Rect(0, 0, 64, 64), Rect(P(10), P(140), 64, 64), white, 10);
  r.drawSprite(tex_[0], Rect(448, 448, 64, 64), Rect(P(80), P(140), 64, 64), white, 10);              // bottom-right corner of 512x512
  r.drawSprite(tex_[1], Rect(0, 0, 64, 64), Rect(P(150), P(140), 64, 64), white, 10);                 // RAM texture
  r.drawSprite(tex_[1], Rect(0, 0, 256, 256), Rect(P(220), P(140), 64, 64), Color32(255, 255, 255, 128), 10);  // minified + translucent
  // clipping at the screen edges
  r.drawSprite(tex_[2], Rect(0, 0, 32, 32), Rect(-10, P(210), 32, 32), white, 10);
  r.drawSprite(tex_[2], Rect(0, 0, 32, 32), Rect(P(480) - 22, P(210), 32, 32), white, 10);
  r.drawSprite(tex_[2], Rect(0, 0, 32, 32), Rect(P(300), P(272) - 10, 32, 32), white, 10);
  // sort order: submitted back to front (3,2,1): drawn 1 (top key) last, so the green rect covers the red one
  r.drawRect(Rect(P(300), P(145), 50, 40), Color32(0, 200, 0, 255), 30);
  r.drawRect(Rect(P(320), P(155), 50, 40), Color32(220, 0, 0, 255), 20);
  r.drawRect(Rect(P(340), P(165), 50, 40), Color32(0, 0, 255, 160), 25);  // translucent between them
  // text
  r.drawText(tex_[8], g_font_, static_cast<i32>(P(10)), static_cast<i32>(P(215)), "ABCDEFG HIJK 0123\nSECOND LINE", white, 40);
  r.drawText(tex_[8], g_font_, static_cast<i32>(P(10)), static_cast<i32>(P(240)), "TINTED TEXT", Color32(255, 80, 80, 255), 40);
  // an overlay quad (HUD style) on top of everything
  r.drawOverlayQuad(static_cast<i32>(P(440)), static_cast<i32>(P(200)), static_cast<i32>(P(30)), static_cast<i32>(P(30)), Color32(255, 255, 0, 200));
  if (big) {
    // second half: 1000 small sprites in the 640x448 space
    for (u32 i = 0; i < 1000; ++i)
      r.drawSprite(tex_[2], Rect(0, 0, 32, 32), Rect(static_cast<float>((i * 37) % 600), static_cast<float>(((i * 91) % 380) + 20), 8, 8),
                   Color32(255, 255, 255, 255), 5 + (i & 3));
    r.drawRect(Rect(0, 0, 640, 2), Color32(255, 255, 255, 255), 100);      // frame of the virtual space
    r.drawRect(Rect(0, 446, 640, 2), Color32(255, 255, 255, 255), 100);
    r.drawRect(Rect(0, 0, 2, 448), Color32(255, 255, 255, 255), 100);
    r.drawRect(Rect(638, 0, 2, 448), Color32(255, 255, 255, 255), 100);
  }
}

}  // namespace rensga
#endif
