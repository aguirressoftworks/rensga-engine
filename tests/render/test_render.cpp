#include "rensga/render/gl_batch.h"
#include "rensga_test.h"

using namespace rensga;

TEST(batch_sorts_by_key_then_texture_then_submission) {
  SpriteBatch b;
  CHECK(b.init(8));
  const Color32 w(255, 255, 255, 255);
  b.add(2, 0, 0, 1, 1, 0, 0, 1, 1, w, 5);  // 0
  b.add(1, 10, 0, 11, 1, 0, 0, 1, 1, w, 5);  // 1: same key, lower texture -> first
  b.add(2, 20, 0, 21, 1, 0, 0, 1, 1, w, 1);  // 2: lowest key -> very first
  b.add(2, 30, 0, 31, 1, 0, 0, 1, 1, w, 5);  // 3: ties keep submission order after 0
  CHECK_EQ(b.build(), 24u);
  const SpriteVertex* v = b.vertices();
  CHECK_EQ(v[0].x, 20.0f);
  CHECK_EQ(v[6].x, 10.0f);
  CHECK_EQ(v[12].x, 0.0f);
  CHECK_EQ(v[18].x, 30.0f);
  CHECK_EQ(b.runCount(), 3u);  // tex 2 | tex 1 | tex 2 (key 5): runs only merge neighbours
  CHECK_EQ(b.runs()[0].tex, 2u);
  CHECK_EQ(b.runs()[1].tex, 1u);
  CHECK_EQ(b.runs()[2].tex, 2u);
  CHECK_EQ(b.runs()[2].count, 12u);
}

TEST(batch_merges_same_texture_into_one_run) {
  SpriteBatch b;
  CHECK(b.init(1000));
  for (u32 i = 0; i < 1000; ++i) b.add(1, 0, 0, 1, 1, 0, 0, 1, 1, Color32(), i & 3);
  CHECK_EQ(b.build(), 6000u);
  CHECK_EQ(b.runCount(), 1u);
  CHECK_EQ(b.runs()[0].count, 6000u);
}

TEST(batch_full_drops_and_counts) {
  SpriteBatch b;
  CHECK(b.init(2));
  CHECK(b.add(1, 0, 0, 1, 1, 0, 0, 1, 1, Color32(), 0));
  CHECK(b.add(1, 0, 0, 1, 1, 0, 0, 1, 1, Color32(), 0));
  CHECK(!b.add(1, 0, 0, 1, 1, 0, 0, 1, 1, Color32(), 0));
  CHECK_EQ(b.dropped(), 1u);
  b.clear();
  CHECK_EQ(b.count(), 0u);
  CHECK(b.add(1, 0, 0, 1, 1, 0, 0, 1, 1, Color32(), 0));
}

TEST(batch_vertex_colors_and_uvs) {
  SpriteBatch b;
  CHECK(b.init(1));
  b.add(3, 4, 5, 8, 9, 0.25f, 0.5f, 0.75f, 1.0f, Color32(10, 20, 30, 40), 0);
  b.build();
  const SpriteVertex* v = b.vertices();
  CHECK_EQ(v[0].x, 4.0f);
  CHECK_EQ(v[0].y, 5.0f);
  CHECK_EQ(v[0].u, 0.25f);
  CHECK_EQ(v[2].u, 0.75f);
  CHECK_EQ(v[2].v, 1.0f);
  CHECK_EQ(v[0].r, 10);
  CHECK_EQ(v[0].a, 40);
}

TEST(letterbox_integer_and_fractional) {
  LetterboxRect r = computeLetterbox(480, 272, 960, 544);
  CHECK(r.integerScale);
  CHECK_EQ(r.w, 960);
  CHECK_EQ(r.h, 544);
  CHECK_EQ(r.x, 0);
  r = computeLetterbox(480, 272, 1280, 720);  // 2x fits (960x544), centered
  CHECK(r.integerScale);
  CHECK_EQ(r.w, 960);
  CHECK_EQ(r.x, 160);
  CHECK_EQ(r.y, 88);
  r = computeLetterbox(640, 448, 320, 300);  // smaller than the image: aspect fit
  CHECK(!r.integerScale);
  CHECK_EQ(r.w, 320);
  CHECK_EQ(r.h, 224);
  CHECK_EQ(r.y, 38);
  r = computeLetterbox(480, 272, 0, 100);
  CHECK_EQ(r.w, 0);
}

TEST(clut_expansion) {
  const u32 pal[16] = {0, 0x11111111u, 0x22222222u, 0x33333333u};
  const u8 c8[4] = {0, 1, 2, 3};
  u32 out[4] = {};
  TextureDesc d;
  d.width = 2; d.height = 2; d.format = tex_format::CLUT8; d.pixels = c8; d.palette = pal;
  CHECK(expandClut(d, 16, out));
  CHECK_EQ(out[1], 0x11111111u);
  CHECK_EQ(out[3], 0x33333333u);
  // CLUT4: low nibble first; rows padded to a whole byte (width 3 -> 2 bytes per row)
  const u8 c4[4] = {0x10, 0x02, 0x13, 0x00};  // row0: 0,1,2  row1: 3,1,0
  u32 o4[6] = {};
  d.width = 3; d.height = 2; d.format = tex_format::CLUT4; d.pixels = c4;
  CHECK(expandClut(d, 16, o4));
  CHECK_EQ(o4[0], 0u);
  CHECK_EQ(o4[1], 0x11111111u);
  CHECK_EQ(o4[2], 0x22222222u);
  CHECK_EQ(o4[3], 0x33333333u);
  CHECK_EQ(o4[4], 0x11111111u);
  d.palette = nullptr;
  CHECK(!expandClut(d, 16, o4));
}
