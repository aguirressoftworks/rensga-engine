// F4.04: host checks of the pieces of the GU 2D backend that need no PSP: the sprite queue (radix sort by
// (sortKey, texture, submission), clipping, scaling, vertex layout), the VRAM map and the first-fit texture allocator.
#include <stddef.h>

#include <algorithm>
#include <vector>

#include "rensga/core/backends.h"
#include "rensga/render/gu_sprite.h"
#include "rensga/render/gu_vram.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct Q {
  std::vector<GuSprite> items;
  std::vector<u16> order;
  std::vector<GuVertex2D> verts;
  std::vector<GuRun> runs;
  GuSpriteQueue q;
  explicit Q(u32 cap) : items(cap), order(2 * cap), verts(2 * cap), runs(cap) { q.init(items.data(), order.data(), cap); }
  u32 build() { return q.build(verts.data(), runs.data()); }
};
const GuViewMap kScreen;  // identity, 480x272

u32 lcg(u32& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
}  // namespace

TEST(gu_vertex_is_16_bytes_in_ge_order) {
  CHECK_EQ(sizeof(GuVertex2D), 16u);
  CHECK_EQ(offsetof(GuVertex2D, u), 0u);   // GU_TEXTURE_16BIT
  CHECK_EQ(offsetof(GuVertex2D, color), 4u);  // GU_COLOR_8888
  CHECK_EQ(offsetof(GuVertex2D, x), 8u);   // GU_VERTEX_16BIT x, y, z
  CHECK_EQ(offsetof(GuVertex2D, z), 12u);
}

TEST(gu_radix_sort_matches_stable_reference) {
  const u32 N = 1500;
  Q q(N);
  struct Ref { u32 key, tex, seq; };
  std::vector<Ref> ref;
  u32 seed = 12345;
  for (u32 i = 0; i < N; ++i) {
    // few distinct keys and textures so ties are common; some keys use the high bytes
    const u32 key = (lcg(seed) % 5 == 0) ? (lcg(seed) << 8) ^ lcg(seed) : lcg(seed) % 6;
    const u32 tex = lcg(seed) % 4 == 0 ? 0 : 1 + lcg(seed) % 255;
    // x encodes the submission index so the output order can be read back from the vertices
    CHECK(q.q.add(tex, 0, 0, 8, 8, static_cast<float>(i % 400), static_cast<float>((i / 400) * 10), static_cast<float>(i % 400) + 4,
                  static_cast<float>((i / 400) * 10) + 4, 0xFFFFFFFFu, key, kScreen));
    ref.push_back(Ref{key, tex, i});
  }
  std::stable_sort(ref.begin(), ref.end(), [](const Ref& a, const Ref& b) {
    if (a.key != b.key) return a.key < b.key;
    return a.tex < b.tex;
  });
  CHECK_EQ(q.q.count(), N);
  const u32 runCount = q.build();
  for (u32 i = 0; i < N; ++i) CHECK_EQ(q.q.orderAt(i), ref[i].seq);
  u32 expectRuns = 0;
  for (u32 i = 0; i < N; ++i) expectRuns += (i == 0 || ref[i].tex != ref[i - 1].tex);
  CHECK_EQ(runCount, expectRuns);
  u32 total = 0;
  for (u32 r = 0; r < runCount; ++r) {
    CHECK_EQ(q.runs[r].first, total);
    total += q.runs[r].count;
  }
  CHECK_EQ(total, 2 * N);
}

TEST(gu_sort_keeps_submission_order_and_merges_runs) {
  Q q(8);
  q.q.add(2, 0, 0, 1, 1, 0, 0, 1, 1, 0xFFFFFFFFu, 5, kScreen);   // 0
  q.q.add(1, 0, 0, 1, 1, 10, 0, 11, 1, 0xFFFFFFFFu, 5, kScreen);  // 1: same key, lower texture first
  q.q.add(2, 0, 0, 1, 1, 20, 0, 21, 1, 0xFFFFFFFFu, 1, kScreen);  // 2: lowest key first of all
  q.q.add(2, 0, 0, 1, 1, 30, 0, 31, 1, 0xFFFFFFFFu, 5, kScreen);  // 3: ties keep submission order after 0
  const u32 runs = q.build();
  CHECK_EQ(q.q.orderAt(0), 2u);
  CHECK_EQ(q.q.orderAt(1), 1u);
  CHECK_EQ(q.q.orderAt(2), 0u);
  CHECK_EQ(q.q.orderAt(3), 3u);
  CHECK_EQ(runs, 3u);  // tex 2 (key 1) | tex 1 | tex 2 (key 5, two sprites)
  CHECK_EQ(q.runs[2].count, 4u);
  CHECK_EQ(q.verts[0].x, 20);
  CHECK_EQ(q.verts[1].x, 21);
}

TEST(gu_sprite_vertices_and_untextured_rect) {
  Q q(4);
  q.q.add(3, 16, 32, 48, 64, 100.4f, 50.6f, 132, 82, 0x80FF0000u, 0, kScreen);
  q.q.add(0, 9, 9, 9, 9, 10, 20, 30, 40, 0xFF00FF00u, 0, kScreen);  // rect: UVs are zeroed
  q.build();
  // tex 0 sorts first
  CHECK_EQ(q.verts[0].x, 10);
  CHECK_EQ(q.verts[0].u, 0);
  CHECK_EQ(q.verts[1].y, 40);
  CHECK_EQ(q.verts[2].x, 100);  // 100.4 rounds to 100
  CHECK_EQ(q.verts[2].y, 51);   // 50.6 rounds to 51
  CHECK_EQ(q.verts[2].u, 16);
  CHECK_EQ(q.verts[3].v, 64);
  CHECK_EQ(q.verts[2].color, 0x80FF0000u);
  CHECK_EQ(q.verts[2].z, 0);
}

TEST(gu_clipping_adjusts_uv_and_drops_offscreen) {
  Q q(8);
  // 64x64 sprite whose left quarter is off screen: x -16..48 shows texel columns 16..64
  CHECK(q.q.add(1, 0, 0, 64, 64, -16, 10, 48, 74, 0xFFFFFFFFu, 0, kScreen));
  // fully off screen: not queued, but not a failure
  CHECK(q.q.add(1, 0, 0, 64, 64, 500, 10, 564, 74, 0xFFFFFFFFu, 0, kScreen));
  CHECK(q.q.add(1, 0, 0, 64, 64, 10, -100, 74, -36, 0xFFFFFFFFu, 0, kScreen));
  CHECK_EQ(q.q.count(), 1u);
  // runs off the right and bottom edges: 440..504 clipped to 440..480 shows the left 40 texel columns
  CHECK(q.q.add(1, 0, 0, 64, 64, 440, 250, 504, 314, 0xFFFFFFFFu, 1, kScreen));
  CHECK_EQ(q.q.count(), 2u);
  q.build();
  CHECK_EQ(q.verts[0].x, 0);
  CHECK_EQ(q.verts[0].u, 16);
  CHECK_EQ(q.verts[1].x, 48);
  CHECK_EQ(q.verts[1].u, 64);
  CHECK_EQ(q.verts[2].x, 440);
  CHECK_EQ(q.verts[3].x, 480);
  CHECK_EQ(q.verts[3].u, 40);
  CHECK_EQ(q.verts[3].y, 272);
  CHECK_EQ(q.verts[3].v, 22);  // 22 of 64 rows fit in 250..272
}

TEST(gu_flip_and_degenerate_sprites) {
  Q q(8);
  // a horizontally mirrored sprite is drawn with u0 > u1
  q.q.add(1, 32, 0, 0, 32, 100, 100, 132, 132, 0xFFFFFFFFu, 0, kScreen);
  // the same by a negative destination width
  q.q.add(1, 0, 0, 32, 32, 232, 100, 200, 132, 0xFFFFFFFFu, 1, kScreen);
  // empty
  q.q.add(1, 0, 0, 0, 0, 5, 5, 5, 20, 0xFFFFFFFFu, 2, kScreen);
  CHECK_EQ(q.q.count(), 2u);
  q.build();
  CHECK_EQ(q.verts[0].x, 100);
  CHECK_EQ(q.verts[0].u, 32);
  CHECK_EQ(q.verts[1].x, 132);
  CHECK_EQ(q.verts[1].u, 0);
  CHECK_EQ(q.verts[2].x, 200);
  CHECK_EQ(q.verts[2].u, 32);
  CHECK_EQ(q.verts[3].x, 232);
  CHECK_EQ(q.verts[3].u, 0);
}

TEST(gu_queue_full_drops_and_counts) {
  Q q(2);
  CHECK(q.q.add(1, 0, 0, 1, 1, 0, 0, 4, 4, 0, 0, kScreen));
  CHECK(q.q.add(1, 0, 0, 1, 1, 0, 0, 4, 4, 0, 0, kScreen));
  CHECK(!q.q.add(1, 0, 0, 1, 1, 0, 0, 4, 4, 0, 0, kScreen));
  CHECK_EQ(q.q.dropped(), 1u);
  CHECK_EQ(q.q.count(), 2u);
  q.q.clear();
  CHECK_EQ(q.q.count(), 0u);
  CHECK(q.q.add(1, 0, 0, 1, 1, 0, 0, 4, 4, 0, 0, kScreen));
}

TEST(gu_view_map_letterboxes_other_resolutions) {
  const GuViewMap id = GuViewMap::fit(480, 272);
  CHECK_NEAR(id.scale, 1.0f, 1e-6f);
  CHECK_NEAR(id.offX, 0.0f, 1e-6f);
  const GuViewMap ps2 = GuViewMap::fit(640, 448);  // PS2 profile on the PSP screen: height limits, centered in x
  CHECK_NEAR(ps2.scale, 272.0f / 448.0f, 1e-5f);
  CHECK_NEAR(ps2.offX, (480.0f - 640.0f * ps2.scale) * 0.5f, 1e-4f);
  CHECK_NEAR(ps2.offY, 0.0f, 1e-4f);
  Q q(2);
  q.q.add(0, 0, 0, 0, 0, 0, 0, 640, 448, 0xFFFFFFFFu, 0, ps2);
  q.build();
  CHECK_EQ(q.verts[0].y, 0);
  CHECK_EQ(q.verts[1].y, 272);
  CHECK(q.verts[0].x > 40 && q.verts[0].x < 50);   // ~46
  CHECK(q.verts[1].x > 430 && q.verts[1].x < 440);  // ~434
  const GuViewMap small = GuViewMap::fit(240, 136);  // smaller space scales up 2x
  CHECK_NEAR(small.scale, 2.0f, 1e-6f);
}

TEST(gu_vram_layout_constants) {
  using namespace gu_vram;
  CHECK_EQ(kFb0, 0u);
  CHECK_EQ(kFb1, 0x44000u);  // 512 x 272 x 2 (5650)
  CHECK_EQ(kDepth, 0x88000u);
  CHECK_EQ(kTexBegin, 0xCC000u);
  CHECK_EQ(kTexEnd, 0x200000u);
  CHECK_EQ(kTexArea, 0x134000u);
}

TEST(gu_vram_first_fit_free_and_merge) {
  GuVramAllocator a;
  a.init(0x1000, 0x1000 + 0x400);
  u32 p, q, r, s;
  CHECK(a.alloc(0x100, &p));
  CHECK(a.alloc(0x100, &q));
  CHECK(a.alloc(0x100, &r));
  CHECK_EQ(p, 0x1000u);
  CHECK_EQ(q, 0x1100u);
  CHECK_EQ(r, 0x1200u);
  CHECK_EQ(a.used(), 0x300u);
  CHECK(!a.alloc(0x200, &s));  // only 0x100 left
  CHECK(a.alloc(0x100, &s));
  CHECK_EQ(s, 0x1300u);
  CHECK(!a.alloc(16, &s));  // full
  a.free(q, 0x100);
  a.free(p, 0x100);  // merges with the hole after it
  CHECK_EQ(a.freeRanges(), 1u);
  CHECK_EQ(a.largestFree(), 0x200u);
  CHECK(a.alloc(0x180, &s));  // first fit: the front hole
  CHECK_EQ(s, 0x1000u);
  a.free(s, 0x180);
  a.free(r, 0x100);  // merges with both neighbours (hole before, and the allocated block s at 0x1300 stays)
  CHECK_EQ(a.largestFree(), 0x300u);
  a.reset();
  CHECK_EQ(a.used(), 0u);
  CHECK_EQ(a.largestFree(), 0x400u);
}

TEST(gu_vram_alloc_rounds_to_16_and_stays_aligned) {
  GuVramAllocator a;
  a.init(gu_vram::kTexBegin, gu_vram::kTexEnd);
  u32 x, y;
  CHECK(a.alloc(1, &x));
  CHECK(a.alloc(17, &y));
  CHECK_EQ(x % 16, 0u);
  CHECK_EQ(y, x + 16);
  CHECK_EQ(a.used(), 16u + 32u);
  CHECK_EQ(a.area(), gu_vram::kTexArea);
}

TEST(gu_vram_texture_budget_example) {
  // 512x512 RGBA8 is 1 MiB: one fits the 1.2 MiB area, the second goes to RAM.
  GuVramAllocator a;
  a.init(gu_vram::kTexBegin, gu_vram::kTexEnd);
  u32 o;
  CHECK(a.alloc(guTextureBytes(tex_format::RGBA8, 512, 512), &o));
  CHECK(!a.alloc(guTextureBytes(tex_format::RGBA8, 512, 512), &o));
  CHECK(a.alloc(guTextureBytes(tex_format::CLUT8, 256, 256), &o));  // small ones still fit
  CHECK_EQ(guTextureBytes(tex_format::CLUT4, 32, 8), 128u);
  CHECK_EQ(guTextureBytes(tex_format::RGBA5551, 16, 16), 512u);
  CHECK_EQ(guTextureBytes(tex_format::RGBA4444, 8, 8), 128u);
}
