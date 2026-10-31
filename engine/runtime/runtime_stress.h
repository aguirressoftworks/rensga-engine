// The 2D sprite stress (desktop --stress-sprites, PSP RENSGA_PSP_STRESS_SPRITES): N moving sprites with per-sprite tint
// and sort key, a CLUT4 ball, a CLUT8 box, a digit font and a frame-time HUD. Header-only, no heap, deterministic.
// `padded` lays the pixels out for the GU rules (CLUT4 rows at least 16 bytes, stored size a power of two, height >= 8)
// and passes storedWidth/storedHeight; the desktop keeps the tight layout it always had.
#pragma once
#include <stdio.h>
#include <string.h>

#include "rensga/core/backends.h"

#ifndef RENSGA_STRESS_MAX
#define RENSGA_STRESS_MAX 16000
#endif

namespace rensga {

class SpriteStress {
 public:
  static constexpr u32 kMax = RENSGA_STRESS_MAX;

  // Returns false if a texture upload fails. `w`/`h` is the virtual screen the sprites bounce in.
  bool setup(IRenderer& r, u32 count, u32 w, u32 h, bool padded) {
    const u32 ballRow = padded ? 16 : 8, digitRow = padded ? 32 : 20, digitRows = padded ? 8 : 6;
    memset(ball_, 0, sizeof(ball_));
    memset(digits_, 0, sizeof(digits_));
    makeBall(ball_, ballPal_, ballRow);
    makeBox(box_, boxPal_);
    makeDigitFont(digits_, digitPal_, digitRow);
    TextureDesc d;
    d.width = d.height = 16;
    d.format = tex_format::CLUT4; d.pixels = ball_; d.palette = ballPal_;
    if (padded) { d.storedWidth = 32; d.storedHeight = 16; }
    ballTex_ = r.uploadTexture(d);
    d.format = tex_format::CLUT8; d.pixels = box_; d.palette = boxPal_;
    if (padded) { d.storedWidth = 16; d.storedHeight = 16; }
    boxTex_ = r.uploadTexture(d);
    d.width = 40; d.height = 6;
    d.format = tex_format::CLUT4; d.pixels = digits_; d.palette = digitPal_;
    if (padded) { d.storedWidth = 64; d.storedHeight = digitRows; }
    fontTex_ = r.uploadTexture(d);
    if (!ballTex_ || !boxTex_ || !fontTex_) return false;
    font_ = PackFontRec();
    font_.glyphW = 4; font_.glyphH = 6; font_.firstChar = '0'; font_.charCount = 10; font_.columns = 10;
    font_.lineHeight = 7;
    for (int i = 0; i < 10; ++i) font_.advance[i] = 4;
    count_ = count < kMax ? count : kMax;
    seed_ = 12345;
    for (u32 i = 0; i < count_; ++i) {
      Sprite& s = sprites_[i];
      s.x = rnd() * static_cast<float>(w - 16);
      s.y = rnd() * static_cast<float>(h - 16);
      s.vx = (rnd() - 0.5f) * 240.0f;
      s.vy = (rnd() - 0.5f) * 240.0f;
      s.tex = i & 1;
      s.r = static_cast<u8>(96 + rnd() * 159);
      s.g = static_cast<u8>(96 + rnd() * 159);
      s.b = static_cast<u8>(96 + rnd() * 159);
    }
    return true;
  }

  // One fixed step (1/60 s); sprites bounce off the virtual screen edges.
  void step(u32 w, u32 h) {
    const float dt = 1.0f / 60.0f, maxX = static_cast<float>(w - 16), maxY = static_cast<float>(h - 16);
    for (u32 i = 0; i < count_; ++i) {
      Sprite& s = sprites_[i];
      s.x += s.vx * dt;
      s.y += s.vy * dt;
      if (s.x < 0) { s.x = 0; s.vx = -s.vx; } else if (s.x > maxX) { s.x = maxX; s.vx = -s.vx; }
      if (s.y < 0) { s.y = 0; s.vy = -s.vy; } else if (s.y > maxY) { s.y = maxY; s.vy = -s.vy; }
    }
  }

  void setFrameMs(float ms) { snprintf(hud_, sizeof(hud_), "%d", static_cast<int>(ms * 10.0f)); }

  void draw(IRenderer& r) const {
    r.drawRect(Rect(0, 0, 480, 20), Color32(20, 40, 80, 255), 0);
    for (u32 i = 0; i < count_; ++i) {
      const Sprite& s = sprites_[i];
      r.drawSprite(s.tex ? boxTex_ : ballTex_, Rect(0, 0, 16, 16), Rect(s.x, s.y, 16, 16), Color32(s.r, s.g, s.b, 255), 10 + (i & 3));
    }
    r.drawRect(Rect(4, 4, 36, 12), Color32(0, 0, 0, 200), 100);
    r.drawText(fontTex_, font_, 6, 7, hud_, Color32(255, 220, 80, 255), 101);
  }

  u32 count() const { return count_; }

 private:
  struct Sprite {
    float x, y, vx, vy;
    u32 tex;  // 0 ball, 1 box
    u8 r, g, b;
  };

  float rnd() {  // deterministic LCG, 0..1
    seed_ = seed_ * 1664525u + 1013904223u;
    return static_cast<float>((seed_ >> 8) & 0xFFFF) / 65536.0f;
  }

  // 16x16 ball as a CLUT4 image: 0 transparent, 1 rim, 2 body, 3 highlight. `rowBytes` is the stored row size.
  static void makeBall(u8* packed, u32* pal, u32 rowBytes) {
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        const float dx = x - 7.5f, dy = y - 7.5f, d2 = dx * dx + dy * dy;
        u8 v = 0;
        if (d2 <= 56.0f) v = d2 > 40.0f ? 1 : ((dx + 3) * (dx + 3) + (dy + 3) * (dy + 3) < 6.0f ? 3 : 2);
        u8& b = packed[y * rowBytes + x / 2];
        b = static_cast<u8>((x & 1) ? (b | (v << 4)) : (b | v));
      }
    for (int i = 0; i < 16; ++i) pal[i] = 0;
    pal[1] = 0xFF404040u;  // R in the low byte
    pal[2] = 0xFFB0B0B0u;
    pal[3] = 0xFFFFFFFFu;
  }

  // 16x16 box as CLUT8: a gradient frame, index = distance from the edge.
  static void makeBox(u8* idx, u32* pal) {
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        const int e = x < y ? x : y, f = (15 - x) < (15 - y) ? (15 - x) : (15 - y);
        const int m = e < f ? e : f;
        idx[y * 16 + x] = static_cast<u8>(m > 3 ? 4 : m);
      }
    for (int i = 0; i < 256; ++i) pal[i] = 0;
    pal[0] = 0xFF303030u; pal[1] = 0xFF808080u; pal[2] = 0xFFB0B0B0u; pal[3] = 0xFFE0E0E0u; pal[4] = 0xFFFFFFFFu;
  }

  // 3x5 digit font in 4x6 cells (CLUT4, 10 cells in one row).
  static void makeDigitFont(u8* packed, u32* pal, u32 rowBytes) {
    static const char* kDigits[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111",
                                      "101101111001001", "111100111001111", "111100111101111", "111001001001001",
                                      "111101111101111", "111101111001111"};
    for (int d = 0; d < 10; ++d)
      for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 3; ++x)
          if (kDigits[d][y * 3 + x] == '1') {
            const int px = d * 4 + x;
            u8& b = packed[y * rowBytes + px / 2];
            b = static_cast<u8>((px & 1) ? (b | 0x10) : (b | 0x01));
          }
    for (int i = 0; i < 16; ++i) pal[i] = 0;
    pal[1] = 0xFFFFFFFFu;
  }

  u32 seed_ = 12345;
  u32 count_ = 0;
  u32 ballTex_ = 0, boxTex_ = 0, fontTex_ = 0;
  PackFontRec font_;
  char hud_[32] = "0";
  alignas(16) u8 ball_[16 * 16];     // 16 rows x up to 16 bytes
  alignas(16) u8 box_[16 * 16];
  alignas(16) u8 digits_[8 * 32];    // up to 8 rows x 32 bytes
  alignas(16) u32 ballPal_[16], boxPal_[256], digitPal_[16];
  Sprite sprites_[kMax];
};

}  // namespace rensga
