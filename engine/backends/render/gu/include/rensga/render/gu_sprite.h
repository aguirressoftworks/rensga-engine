// F4.04: the GU 2D sprite queue. Plain C++ (no PSP headers) so tests/render checks it on the host.
// Sprites, rects and text glyphs become GU_SPRITES pairs (top-left, bottom-right) in through mode: 16-byte vertices
// with 16-bit texel UVs. A static LSD radix sort orders them by (sortKey, texture, submission order).
#pragma once
#include "rensga/core/types.h"

namespace rensga {

// GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D. The GE reads u,v (4 B), color (4 B), x,y,z (6 B)
// and the vertex is padded to the 4-byte alignment of the color, so one vertex is 16 bytes.
struct GuVertex2D {
  i16 u, v;
  u32 color;  // ABGR
  i16 x, y, z;
  i16 pad;
};
static_assert(sizeof(GuVertex2D) == 16, "GuVertex2D is 16 bytes");

// One queued sprite: two vertices and its sort key. tex 0 = untextured rect.
struct GuSprite {
  u32 sortKey;
  u32 tex;
  GuVertex2D a, b;
};

// Consecutive sprites with the same texture: `count` vertices from vertex index `first`.
struct GuRun {
  u32 tex;
  u32 first;
  u32 count;
};

// Maps virtual (game) pixels to the 480x272 screen: uniform scale and a centering offset (letterbox).
struct GuViewMap {
  float scale = 1.0f, offX = 0.0f, offY = 0.0f;
  i32 screenW = 480, screenH = 272;
  static GuViewMap fit(u32 virtualW, u32 virtualH, u32 screenW = 480, u32 screenH = 272);
};

class GuSpriteQueue {
 public:
  static constexpr u32 kMaxTexture = 255;  // handles are 1..255 (one radix byte)
  // Storage is handed in so the owner decides where it lives (static arrays on the PSP, no heap, small object).
  // `order` points at 2 * capacity u16 values (sort ping-pong).
  void init(GuSprite* items, u16* order, u32 capacity);
  void clear() { count_ = 0; }
  // Queues one sprite. src* are texel corners (x0,y0)-(x1,y1) (ignored for tex 0), dst* virtual-pixel corners.
  // Clips to the screen (through-mode coordinates must stay inside it), scales by `map`, rounds to whole pixels.
  // Returns false when the sprite was dropped because the queue is full; sprites that are off screen or empty
  // return true (nothing to draw).
  bool add(u32 tex, float sx0, float sy0, float sx1, float sy1, float dx0, float dy0, float dx1, float dy1,
           u32 colorABGR, u32 sortKey, const GuViewMap& map);
  // Sorts the queue, writes 2 vertices per sprite into `out` (2 * count() entries) and merges runs of one texture.
  // Returns the run count (<= count()).
  u32 build(GuVertex2D* out, GuRun* runs);
  u32 count() const { return count_; }
  u32 capacity() const { return capacity_; }
  u32 dropped() const { return dropped_; }  // total sprites refused since init
  // Order after build(): index of the i-th drawn sprite.
  u32 orderAt(u32 i) const { return order_[i]; }

 private:
  GuSprite* items_ = nullptr;
  u16* order_ = nullptr;  // sorted result after build()
  u16* tmp_ = nullptr;
  u32 capacity_ = 0, count_ = 0, dropped_ = 0;
};

}  // namespace rensga
