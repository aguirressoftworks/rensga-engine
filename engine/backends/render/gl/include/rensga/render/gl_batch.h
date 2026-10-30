// Pure helpers of the GL renderer (no GL calls, unit-tested in tests/render):
// the 2D sprite batch, the letterbox math and CLUT expansion.
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

struct SpriteVertex {
  float x, y, u, v;
  u8 r, g, b, a;
};
static_assert(sizeof(SpriteVertex) == 20, "SpriteVertex is 20 bytes");

// One draw call: `count` vertices starting at `first`, all with texture `tex`.
struct BatchRun {
  u32 tex;
  u32 first;
  u32 count;
};

// Fixed-capacity sprite queue. Everything is allocated in init(); add/build never touch the heap.
// build() orders the queued quads by (sortKey, texture, submission order) and emits two triangles per quad,
// then merges neighbours with the same texture into runs.
class SpriteBatch {
 public:
  ~SpriteBatch() { shutdown(); }
  bool init(u32 capacity);
  void shutdown();
  void clear() { count_ = 0; }
  // Returns false (and counts a drop) when the batch is full.
  bool add(u32 tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color32 tint,
           u32 sortKey);
  // Sorts and fills vertices()/runs(). Returns the vertex count.
  u32 build();
  const SpriteVertex* vertices() const { return verts_; }
  const BatchRun* runs() const { return runs_; }
  u32 runCount() const { return runCount_; }
  u32 count() const { return count_; }
  u32 capacity() const { return capacity_; }
  u32 dropped() const { return dropped_; }  // total quads refused since init

 private:
  struct Quad {
    float x0, y0, x1, y1, u0, v0, u1, v1;
    u32 color;  // ABGR
  };
  struct Order {
    u32 sortKey, tex, index;
  };
  Quad* quads_ = nullptr;
  Order* order_ = nullptr;
  SpriteVertex* verts_ = nullptr;
  BatchRun* runs_ = nullptr;
  u32 capacity_ = 0, count_ = 0, runCount_ = 0, dropped_ = 0;
};

// Where the virtual-resolution image lands in the window (pixels, origin top-left of the window).
struct LetterboxRect {
  i32 x = 0, y = 0, w = 0, h = 0;
  bool integerScale = false;  // true when the image is scaled by a whole number (nearest filtering looks exact)
};
// Largest whole-number scale that fits; when the window is smaller than the image, an aspect-correct
// fractional fit. The image is centered and the rest of the window is black.
LetterboxRect computeLetterbox(u32 virtualW, u32 virtualH, u32 windowW, u32 windowH);

// Expands a CLUT8 / CLUT4 image (layout in TextureDesc) to RGBA8 into `out` (width*height u32).
// Returns false on a missing palette or a bad format. Indices past the palette read as palette[0].
bool expandClut(const TextureDesc& desc, u32 paletteCount, u32* out);

}  // namespace rensga
