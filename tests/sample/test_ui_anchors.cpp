// F4.01 UI anchors: x / y are offsets from the anchor point of the CURRENT view, so one set of scenes fits 640x448 (PS2) and 480x272
// (PSP). The math is checked for all nine anchors, then every scene of the cooked sample is drawn by renderGameplay2D into a recording
// renderer at both sizes and every text and widget must lie inside the view.
#include <algorithm>

#include "sample_harness.h"

using namespace sample;

TEST(ui_anchor_math_places_the_pivot_on_the_anchor_point) {
  const i32 W = 480, H = 272, w = 40, h = 10;
  // x = 7, y = -3 from each anchor; the element's own matching corner / edge / centre sits there.
  const i32 ax[9] = {0, W / 2, W, 0, W / 2, W, 0, W / 2, W};
  const i32 ay[9] = {0, 0, 0, H / 2, H / 2, H / 2, H, H, H};
  const i32 px[9] = {0, w / 2, w, 0, w / 2, w, 0, w / 2, w};
  const i32 py[9] = {0, 0, 0, h / 2, h / 2, h / 2, h, h, h};
  for (i32 a = 0; a < 9; ++a) {
    const Rect r = UI::resolveRect(a, 7, -3, w, h, W, H);
    CHECK_NEAR(r.x, static_cast<float>(ax[a] + 7 - px[a]), 0.001f);
    CHECK_NEAR(r.y, static_cast<float>(ay[a] - 3 - py[a]), 0.001f);
    CHECK_NEAR(r.width, static_cast<float>(w), 0.001f);
    CHECK_NEAR(r.height, static_cast<float>(h), 0.001f);
  }
  // TopLeft is the old behaviour at any view size; an unknown anchor falls back to it.
  CHECK_NEAR(UI::resolveRect(0, 12, 34, 5, 6, 640, 448).x, 12.0f, 0.001f);
  CHECK_NEAR(UI::resolveRect(0, 12, 34, 5, 6, 480, 272).y, 34.0f, 0.001f);
  CHECK_NEAR(UI::resolveRect(99, 12, 34, 5, 6, 480, 272).x, 12.0f, 0.001f);
  CHECK_NEAR(UI::resolveRect(-1, 12, 34, 5, 6, 480, 272).y, 34.0f, 0.001f);
  // The same offsets in a bigger view move the anchored element with the edge, not with the origin.
  CHECK_NEAR(UI::resolveRect(UIAnchor::BottomRight, -10, -10, 20, 8, 640, 448).x, 610.0f, 0.001f);
  CHECK_NEAR(UI::resolveRect(UIAnchor::BottomRight, -10, -10, 20, 8, 480, 272).x, 450.0f, 0.001f);
  CHECK_NEAR(UI::resolveRect(UIAnchor::BottomRight, -10, -10, 20, 8, 480, 272).y, 254.0f, 0.001f);
}

TEST(ui_anchor_field_is_reflected_and_defaults_to_top_left) {
  UIText t;
  UIImage i;
  UIButton b;
  CHECK_EQ(t.anchor, 0);
  CHECK_EQ(i.anchor, 0);
  CHECK_EQ(b.anchor, 0);
  const TypeInfo* ti = TypeRegistry::instance().find("UIButton");
  CHECK(ti != nullptr);
  bool found = false;
  if (ti) for (u32 f = 0; f < ti->fieldCount; ++f) if (!strcmp(ti->fields[f].name, "anchor")) found = true;
  CHECK(found);
}

namespace {
void checkSceneAtSize(u32 scene, u32 w, u32 h, const char* label) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("anchors"), scene));
  g.fakeUploads();   // renderGameplay2D skips UI whose texture handle is 0
  g.run(6);
  RecordingRenderer rr;
  rr.w = w;
  rr.h = h;
  renderGameplay2D(*World::active(), rr);
  CHECK(!rr.rects.empty());
  bool covered = false;
  for (const RecordingRenderer::Rec& r : rr.rects) {
    if (r.isText) {
      CHECK(r.tx >= 0 && r.ty >= 0);
      if (!(r.tx + r.tw <= static_cast<i32>(w) && r.ty + r.th <= static_cast<i32>(h))) {
        fprintf(stderr, "    [%s %ux%u] text \"%s\" at %d,%d size %dx%d leaves the view\n", label, w, h, r.text, r.tx, r.ty, r.tw, r.th);
        ++rtest::failures();
      }
      continue;
    }
    if (r.r.x <= 0.0f && r.r.y <= 0.0f && r.r.x + r.r.width >= static_cast<float>(w) && r.r.y + r.r.height >= static_cast<float>(h)) {
      covered = true;   // a full-screen backdrop is allowed to be bigger than the view
      continue;
    }
    if (!(r.r.x >= 0.0f && r.r.y >= 0.0f && r.r.x + r.r.width <= static_cast<float>(w) && r.r.y + r.r.height <= static_cast<float>(h))) {
      fprintf(stderr, "    [%s %ux%u] widget %.0f,%.0f %.0fx%.0f leaves the view\n", label, w, h, r.r.x, r.r.y, r.r.width, r.r.height);
      ++rtest::failures();
    }
  }
  if (scene == kTitle || scene == kGameOver || scene == kResults || scene == kInputTest) CHECK(covered);
}
}  // namespace

TEST(ui_every_scene_fits_640x448_and_480x272) {
  static const char* const names[9] = {"Title", "Room", "Room2", "Room3", "GameOver", "Results", "TitleBot", "InputTest", "RoomBot"};
  for (u32 scene = 0; scene < 9; ++scene) {
    checkSceneAtSize(scene, 640, 448, names[scene]);
    checkSceneAtSize(scene, 480, 272, names[scene]);
  }
}

TEST(ui_hud_hangs_from_the_view_edges_at_both_sizes) {
  const u32 sizes[2][2] = {{640, 448}, {480, 272}};
  for (const auto& s : sizes) {
    Game g;
    CHECK(g.boot(cookSample(), tempDir("hud_edges"), kRoom));
    g.fakeUploads();
    g.run(6);
    RecordingRenderer rr;
    rr.w = s[0];
    rr.h = s[1];
    renderGameplay2D(*World::active(), rr);
    const RecordingRenderer::Rec *coins = nullptr, *time = nullptr, *lives = nullptr, *msg = nullptr;
    for (const RecordingRenderer::Rec& r : rr.rects) {
      if (!r.isText) continue;
      if (!strncmp(r.text, "COINS", 5)) coins = &r;
      if (!strncmp(r.text, "TIME", 4)) time = &r;
      if (!strncmp(r.text, "LIVES", 5)) lives = &r;
    }
    CHECK(coins && time && lives);
    if (!coins || !time || !lives) continue;
    CHECK_EQ(coins->tx, 10);                                              // TopLeft
    CHECK_EQ(time->tx + time->tw, static_cast<i32>(s[0]) - 10);           // TopRight: 10 px from the right edge
    CHECK_EQ(lives->tx + lives->tw / 2, static_cast<i32>(s[0]) / 2);      // Top: centred
    CHECK_EQ(time->ty, 9);
    GameState::Message("ROOM CLEARED");
    g.frame();
    renderGameplay2D(*World::active(), rr);
    for (const RecordingRenderer::Rec& r : rr.rects)
      if (r.isText && !strncmp(r.text, "ROOM CLEARED", 12)) {
        msg = &r;
        CHECK_EQ(r.tx + r.tw / 2, static_cast<i32>(s[0]) / 2);            // Center
        CHECK_EQ(r.ty + r.th / 2, static_cast<i32>(s[1]) / 2 - 56);
      }
    CHECK(msg != nullptr);
  }
}
