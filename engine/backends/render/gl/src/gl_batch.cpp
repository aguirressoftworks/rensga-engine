#include "rensga/render/gl_batch.h"

#include <algorithm>
#include <new>

namespace rensga {

bool SpriteBatch::init(u32 capacity) {
  shutdown();
  if (!capacity) return false;
  quads_ = new (std::nothrow) Quad[capacity];
  order_ = new (std::nothrow) Order[capacity];
  verts_ = new (std::nothrow) SpriteVertex[static_cast<size_t>(capacity) * 6];
  runs_ = new (std::nothrow) BatchRun[capacity];
  if (!quads_ || !order_ || !verts_ || !runs_) {
    shutdown();
    return false;
  }
  capacity_ = capacity;
  return true;
}

void SpriteBatch::shutdown() {
  delete[] quads_;
  delete[] order_;
  delete[] verts_;
  delete[] runs_;
  quads_ = nullptr;
  order_ = nullptr;
  verts_ = nullptr;
  runs_ = nullptr;
  capacity_ = count_ = runCount_ = dropped_ = 0;
}

bool SpriteBatch::add(u32 tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      Color32 tint, u32 sortKey) {
  if (count_ >= capacity_) {
    ++dropped_;
    return false;
  }
  quads_[count_] = Quad{x0, y0, x1, y1, u0, v0, u1, v1, tint.packedABGR()};
  order_[count_] = Order{sortKey, tex, count_};
  ++count_;
  return true;
}

u32 SpriteBatch::build() {
  runCount_ = 0;
  if (!count_) return 0;
  std::sort(order_, order_ + count_, [](const Order& a, const Order& b) {
    if (a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
    if (a.tex != b.tex) return a.tex < b.tex;
    return a.index < b.index;  // submission order breaks ties: the result is deterministic
  });
  u32 n = 0;
  for (u32 i = 0; i < count_; ++i) {
    const Order& o = order_[i];
    const Quad& q = quads_[o.index];
    const u8 r = static_cast<u8>(q.color), g = static_cast<u8>(q.color >> 8), b = static_cast<u8>(q.color >> 16),
             a = static_cast<u8>(q.color >> 24);
    SpriteVertex* v = verts_ + n;
    v[0] = SpriteVertex{q.x0, q.y0, q.u0, q.v0, r, g, b, a};
    v[1] = SpriteVertex{q.x1, q.y0, q.u1, q.v0, r, g, b, a};
    v[2] = SpriteVertex{q.x1, q.y1, q.u1, q.v1, r, g, b, a};
    v[3] = v[0];
    v[4] = v[2];
    v[5] = SpriteVertex{q.x0, q.y1, q.u0, q.v1, r, g, b, a};
    if (runCount_ && runs_[runCount_ - 1].tex == o.tex) {
      runs_[runCount_ - 1].count += 6;
    } else {
      runs_[runCount_++] = BatchRun{o.tex, n, 6};
    }
    n += 6;
  }
  return n;
}

LetterboxRect computeLetterbox(u32 vw, u32 vh, u32 ww, u32 wh) {
  LetterboxRect r;
  if (!vw || !vh || !ww || !wh) return r;
  const u32 sx = ww / vw, sy = wh / vh;
  const u32 s = sx < sy ? sx : sy;
  if (s >= 1) {
    r.w = static_cast<i32>(vw * s);
    r.h = static_cast<i32>(vh * s);
    r.integerScale = true;
  } else {
    // Smaller than the image: fit by aspect ratio (cross-multiplied, no floats).
    if (static_cast<u64>(ww) * vh <= static_cast<u64>(wh) * vw) {
      r.w = static_cast<i32>(ww);
      r.h = static_cast<i32>(static_cast<u64>(ww) * vh / vw);
    } else {
      r.h = static_cast<i32>(wh);
      r.w = static_cast<i32>(static_cast<u64>(wh) * vw / vh);
    }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
  }
  r.x = (static_cast<i32>(ww) - r.w) / 2;
  r.y = (static_cast<i32>(wh) - r.h) / 2;
  return r;
}

bool expandClut(const TextureDesc& d, u32 paletteCount, u32* out) {
  if (!d.pixels || !d.palette || !paletteCount || !out) return false;
  const u8* src = static_cast<const u8*>(d.pixels);
  if (d.format == tex_format::CLUT8) {
    for (u32 y = 0; y < d.height; ++y)
      for (u32 x = 0; x < d.width; ++x) {
        u32 i = src[y * d.width + x];
        out[y * d.width + x] = d.palette[i < paletteCount ? i : 0];
      }
    return true;
  }
  if (d.format == tex_format::CLUT4) {
    const u32 rowBytes = (d.width + 1) / 2;
    for (u32 y = 0; y < d.height; ++y)
      for (u32 x = 0; x < d.width; ++x) {
        u8 b = src[y * rowBytes + x / 2];
        u32 i = (x & 1) ? (b >> 4) : (b & 15);
        out[y * d.width + x] = d.palette[i < paletteCount ? i : 0];
      }
    return true;
  }
  return false;
}

}  // namespace rensga
