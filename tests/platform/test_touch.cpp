#include <SDL.h>

#include "rensga/platform/sdl.h"
#include "rensga/platform/touch.h"
#include "rensga_test.h"

using namespace rensga;

static void center(const Rect& r, float* x, float* y) {
  *x = r.x + r.width * 0.5f;
  *y = r.y + r.height * 0.5f;
}

TEST(touch_pad_zones_map_to_buttons) {
  TouchPad t;
  t.setView(640, 448, 1280, 896);  // exact 2x: no letterbox
  float x, y;
  VirtualGamepad p;
  center(t.zones().south, &x, &y);
  t.fingerDown(1, x / 640.0f, y / 448.0f);
  t.apply(p);
  CHECK(p.held.south && !p.held.east && !p.held.left && !p.held.start);
  center(t.zones().east, &x, &y);
  t.fingerMove(1, x / 640.0f, y / 448.0f);
  p = VirtualGamepad();
  t.apply(p);
  CHECK(p.held.east && !p.held.south);
  t.fingerUp(1);
  p = VirtualGamepad();
  t.apply(p);  // released fingers count for the poll they ended in
  CHECK(p.held.east);
  p = VirtualGamepad();
  t.apply(p);
  CHECK(!p.held.east && !p.held.south);
}

TEST(touch_pad_dpad_and_multitouch) {
  TouchPad t;
  t.setView(640, 448, 640, 448);
  const Rect d = t.zones().dpad;
  t.fingerDown(7, (d.x + 4) / 640.0f, (d.y + d.height * 0.5f) / 448.0f);  // far left, vertical centre
  float x, y;
  center(t.zones().south, &x, &y);
  t.fingerDown(8, x / 640.0f, y / 448.0f);
  center(t.zones().start, &x, &y);
  t.fingerDown(9, x / 640.0f, y / 448.0f);
  VirtualGamepad p;
  t.apply(p);
  CHECK(p.held.left && !p.held.right && !p.held.up && !p.held.down);
  CHECK(p.held.south && p.held.start);
  t.fingerUp(7);
  t.fingerUp(8);
  t.fingerUp(9);
  p = VirtualGamepad();
  t.apply(p);
  p = VirtualGamepad();
  t.apply(p);
  CHECK(!p.held.left && !p.held.south && !p.held.start);
}

TEST(touch_pad_letterbox_is_accounted_for) {
  TouchPad t;
  t.setView(640, 448, 1280, 1200);  // 2x scale, 152 px black bars above and below
  float x, y;
  center(t.zones().south, &x, &y);
  VirtualGamepad p;
  t.fingerDown(1, (x * 2.0f) / 1280.0f, (152.0f + y * 2.0f) / 1200.0f);
  t.apply(p);
  CHECK(p.held.south);
  t.fingerUp(1);
  t.apply(p);
  // The same pixel without the bar offset lands 76 virtual px higher: not on South.
  t.fingerDown(2, (x * 2.0f) / 1280.0f, (y * 2.0f) / 1200.0f);
  p = VirtualGamepad();
  t.apply(p);
  CHECK(!p.held.south);
}

// ---- SDL events through SdlPlatform (needs a window) ---------------------------------------------------------
static void pushFinger(Uint32 type, SDL_FingerID id, float nx, float ny) {
  SDL_Event e;
  SDL_zero(e);
  e.type = type;
  e.tfinger.fingerId = id;
  e.tfinger.x = nx;
  e.tfinger.y = ny;
  SDL_PushEvent(&e);
}

TEST(touch_sdl_events_fill_the_pad) {
  static SdlPlatform plat;
  if (!plat.init()) {
    fprintf(stderr, "  CHECK failed: SdlPlatform::init (no display?)\n");
    ++rtest::failures();
    return;
  }
  plat.setVirtualResolution(640, 448);
  VirtualGamepad pad;
  plat.pollInput(pad);  // learns the window size
  SDL_Window* w = SDL_GL_GetCurrentWindow();
  int dw = 0, dh = 0;
  SDL_GL_GetDrawableSize(w, &dw, &dh);
  int sc = dw / 640 < dh / 448 ? dw / 640 : dh / 448;
  if (sc < 1) sc = 1;
  const float ox = (dw - 640.0f * sc) * 0.5f, oy = (dh - 448.0f * sc) * 0.5f;
  auto nx = [&](float vx) { return (ox + vx * sc) / static_cast<float>(dw); };
  auto ny = [&](float vy) { return (oy + vy * sc) / static_cast<float>(dh); };
  float x, y;
  const Rect d = plat.touchZones().dpad;
  pushFinger(SDL_FINGERDOWN, 1, nx(d.x + d.width - 4), ny(d.y + d.height * 0.5f));  // right edge of the d-pad
  center(plat.touchZones().south, &x, &y);
  pushFinger(SDL_FINGERDOWN, 2, nx(x), ny(y));
  center(plat.touchZones().start, &x, &y);
  pushFinger(SDL_FINGERDOWN, 3, nx(x), ny(y));
  plat.pollInput(pad);
  CHECK(pad.held.right && !pad.held.left);
  CHECK(pad.held.south && pad.held.start && !pad.held.east);
  CHECK(plat.touchOverlayVisible());
  pushFinger(SDL_FINGERUP, 2, nx(x), ny(y));
  pushFinger(SDL_FINGERUP, 3, nx(x), ny(y));
  plat.pollInput(pad);  // fingers 2, 3 gone only after this poll reports them
  pushFinger(SDL_FINGERUP, 1, 0, 0);
  plat.pollInput(pad);
  plat.pollInput(pad);
  CHECK(!pad.held.right && !pad.held.south && !pad.held.start);
  plat.shutdown();
}
