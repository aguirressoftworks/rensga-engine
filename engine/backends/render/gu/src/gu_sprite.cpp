// F4.04 (agent "gu"): sprite queue of the GU 2D path. No PSP headers: also compiled into the host test.
#include "rensga/render/gu_sprite.h"

namespace rensga {

namespace {
inline i32 roundToInt(float v) { return static_cast<i32>(v >= 0.0f ? v + 0.5f : v - 0.5f); }
inline i16 clampI16(i32 v) { return static_cast<i16>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }
}  // namespace

GuViewMap GuViewMap::fit(u32 vw, u32 vh, u32 sw, u32 sh) {
  GuViewMap m;
  m.screenW = static_cast<i32>(sw);
  m.screenH = static_cast<i32>(sh);
  if (!vw || !vh || (vw == sw && vh == sh)) return m;
  const float sx = static_cast<float>(sw) / static_cast<float>(vw), sy = static_cast<float>(sh) / static_cast<float>(vh);
  m.scale = sx < sy ? sx : sy;
  m.offX = (static_cast<float>(sw) - static_cast<float>(vw) * m.scale) * 0.5f;
  m.offY = (static_cast<float>(sh) - static_cast<float>(vh) * m.scale) * 0.5f;
  return m;
}

void GuSpriteQueue::init(GuSprite* items, u16* order, u32 capacity) {
  items_ = items;
  order_ = order;
  tmp_ = order + capacity;
  capacity_ = capacity > 65535u ? 65535u : capacity;
  count_ = 0;
  dropped_ = 0;
}

bool GuSpriteQueue::add(u32 tex, float sx0, float sy0, float sx1, float sy1, float dx0, float dy0, float dx1, float dy1,
                        u32 color, u32 sortKey, const GuViewMap& m) {
  if (tex > kMaxTexture) return true;  // not a handle of ours
  // Screen space (float), then normalize so x0 < x1 and y0 < y1; a flip lives in the UV order.
  float x0 = dx0 * m.scale + m.offX, x1 = dx1 * m.scale + m.offX;
  float y0 = dy0 * m.scale + m.offY, y1 = dy1 * m.scale + m.offY;
  float u0 = sx0, u1 = sx1, v0 = sy0, v1 = sy1;
  if (x1 < x0) { float t = x0; x0 = x1; x1 = t; t = u0; u0 = u1; u1 = t; }
  if (y1 < y0) { float t = y0; y0 = y1; y1 = t; t = v0; v0 = v1; v1 = t; }
  const float sw = static_cast<float>(m.screenW), sh = static_cast<float>(m.screenH);
  if (x1 <= 0.0f || y1 <= 0.0f || x0 >= sw || y0 >= sh || x1 <= x0 || y1 <= y0) return true;  // off screen / empty
  // Clip (linear in UV). Nearest sampling of pixel art tolerates the rounding of the clipped UV.
  const float dx = x1 - x0, dy = y1 - y0;
  if (x0 < 0.0f) { u0 += (u1 - u0) * (-x0 / dx); x0 = 0.0f; }
  if (x1 > sw) { u1 = u0 + (u1 - u0) * ((sw - x0) / (x1 - x0)); x1 = sw; }
  if (y0 < 0.0f) { v0 += (v1 - v0) * (-y0 / dy); y0 = 0.0f; }
  if (y1 > sh) { v1 = v0 + (v1 - v0) * ((sh - y0) / (y1 - y0)); y1 = sh; }
  const i32 ix0 = roundToInt(x0), ix1 = roundToInt(x1), iy0 = roundToInt(y0), iy1 = roundToInt(y1);
  if (ix1 <= ix0 || iy1 <= iy0) return true;  // thinner than a pixel after rounding
  if (count_ >= capacity_) {
    ++dropped_;
    return false;
  }
  GuSprite& s = items_[count_];
  s.sortKey = sortKey;
  s.tex = tex;
  const bool textured = tex != 0;
  s.a = GuVertex2D{textured ? clampI16(roundToInt(u0)) : i16(0), textured ? clampI16(roundToInt(v0)) : i16(0), color,
                   clampI16(ix0), clampI16(iy0), 0, 0};
  s.b = GuVertex2D{textured ? clampI16(roundToInt(u1)) : i16(0), textured ? clampI16(roundToInt(v1)) : i16(0), color,
                   clampI16(ix1), clampI16(iy1), 0, 0};
  ++count_;
  return true;
}

u32 GuSpriteQueue::build(GuVertex2D* out, GuRun* runs) {
  const u32 n = count_;
  if (!n) return 0;
  // LSD radix sort, least significant first: texture, then the four bytes of sortKey. Each pass is stable, so
  // submission order survives as the last tie-break. Passes whose digit is the same for every sprite are skipped.
  u16* src = order_;
  u16* dst = tmp_;
  for (u32 i = 0; i < n; ++i) src[i] = static_cast<u16>(i);
  for (u32 pass = 0; pass < 5; ++pass) {
    u32 hist[256] = {};
    for (u32 i = 0; i < n; ++i) {
      const GuSprite& s = items_[src[i]];
      const u32 d = pass == 0 ? (s.tex & 0xFFu) : ((s.sortKey >> (8 * (pass - 1))) & 0xFFu);
      ++hist[d];
    }
    u32 distinct = 0;
    for (u32 d = 0; d < 256; ++d) distinct += hist[d] != 0;
    if (distinct <= 1) continue;
    u32 sum = 0;
    for (u32 d = 0; d < 256; ++d) { const u32 c = hist[d]; hist[d] = sum; sum += c; }
    for (u32 i = 0; i < n; ++i) {
      const GuSprite& s = items_[src[i]];
      const u32 d = pass == 0 ? (s.tex & 0xFFu) : ((s.sortKey >> (8 * (pass - 1))) & 0xFFu);
      dst[hist[d]++] = src[i];
    }
    u16* t = src; src = dst; dst = t;
  }
  if (src != order_) for (u32 i = 0; i < n; ++i) order_[i] = src[i];  // result always in order_
  u32 runCount = 0;
  for (u32 i = 0; i < n; ++i) {
    const GuSprite& s = items_[order_[i]];
    out[2 * i] = s.a;
    out[2 * i + 1] = s.b;
    if (runCount && runs[runCount - 1].tex == s.tex) {
      runs[runCount - 1].count += 2;
    } else {
      runs[runCount++] = GuRun{s.tex, 2 * i, 2};
    }
  }
  return runCount;
}

}  // namespace rensga
