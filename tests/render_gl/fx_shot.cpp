// F6.09 GL check: two cameras (the second in a viewport rectangle with its own clear color and a culling mask), a ParticleSystem, a
// LineRenderer and Debug::DrawLine drawn through renderWorld on the GlRenderer. Writes a PNG and checks pixels. Usage: fx_shot out.png
#include <stdio.h>
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga/core/loop.h"
#include "rensga/core/render_view.h"
#include "rensga/core/world.h"
#include "rensga/platform/sdl.h"
#include "rensga/render/gl.h"

using namespace rensga;

static int g_fail = 0;
#define EXPECT(c)                                                           \
  do {                                                                      \
    if (!(c)) { ++g_fail; fprintf(stderr, "fx-check FAILED: %s (line %d)\n", #c, __LINE__); } \
  } while (0)

static const u8* px(const u8* img, int x, int y) { return img + (static_cast<size_t>(y) * 480 + x) * 4; }
static bool isColor(const u8* p, int r, int g, int b, int tol = 3) {
  return (p[0] > r ? p[0] - r : r - p[0]) <= tol && (p[1] > g ? p[1] - g : g - p[1]) <= tol && (p[2] > b ? p[2] - b : b - p[2]) <= tol;
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "fx.png";
  Arena arena;
  arena.create(1 << 20);
  World world;
  WorldConfig wc;
  wc.maxObjects = 16;
  wc.maxComponentsPerType = 8;
  world.init(wc, arena);

  GameObject* c0 = world.createObject("Main");
  c0->transform.setPosition(Vector3(0, 0, 10));
  c0->AddComponent<Camera>();
  GameObject* c1 = world.createObject("Mini");
  c1->transform.setPosition(Vector3(0, 0, 10));
  Camera* mini = c1->AddComponent<Camera>();
  mini->depth = 1;
  mini->viewportX = 0.5f;  // right half, lower half (viewport y counts from the bottom)
  mini->viewportY = 0.0f;
  mini->viewportW = 0.5f;
  mini->viewportH = 0.5f;
  mini->backgroundColor = Color32(0, 140, 0, 255);
  mini->cullingMask = static_cast<i32>(~(1u << 5));  // layer 5 is hidden in the mini view
  GameObject* l = world.createObject("Line");
  LineRenderer* lr = l->AddComponent<LineRenderer>();
  lr->SetPosition(0, Vector3(-4, 3, 0));
  lr->SetPosition(1, Vector3(4, 3, 0));
  lr->width = 0.5f;
  lr->color = Color32(255, 0, 0, 255);
  GameObject* hidden = world.createObject("HiddenInMini");
  hidden->layer = 5;
  LineRenderer* hl = hidden->AddComponent<LineRenderer>();
  hl->SetPosition(0, Vector3(-4, -3, 0));
  hl->SetPosition(1, Vector3(4, -3, 0));
  hl->width = 0.5f;
  hl->color = Color32(0, 0, 255, 255);
  GameObject* p = world.createObject("Burst");
  p->transform.setPosition(Vector3(-4, 0, 0));
  ParticleSystem* ps = p->AddComponent<ParticleSystem>();
  ps->playOnAwake = false;
  ps->startSize = 2.0f;
  ps->startSpeed = 0;
  ps->shape = 5;
  ps->startLifetime = 5.0f;
  ps->startColor = Color32(255, 255, 0, 255);
  ps->Emit(1);
  Debug::DrawLine(Vector3(0, -1, 0), Vector3(0, 1, 0), Color32(255, 255, 255, 255), 10.0f);

  static SdlPlatform platform;
  static GlRenderer r;
  if (!platform.init() || !r.init(480, 272)) return 3;
  for (int f = 0; f < 3; ++f) renderWorld(world, r, Color32(10, 20, 80, 255));
  static u8 img[480 * 272 * 4];
  if (!r.readPixels(img) || !r.screenshot(out)) return 4;

  // Main camera: the whole screen has the caller's clear color (the camera keeps the default background).
  EXPECT(isColor(px(img, 20, 20), 10, 20, 80));
  EXPECT(isColor(px(img, 100, 200), 10, 20, 80));
  // The mini camera clears its rectangle (x 240..479, y 136..271) to green and draws there.
  EXPECT(isColor(px(img, 400, 230), 0, 140, 0));
  EXPECT(isColor(px(img, 250, 140), 0, 140, 0));
  EXPECT(isColor(px(img, 239, 230), 10, 20, 80));  // just outside the rectangle
  // The red line (y = 3 at depth 10: well above the centre) is drawn in the main view.
  const Vector3 s = Camera::main()->WorldToScreenPoint(Vector3(2, 3, 0));
  EXPECT(isColor(px(img, static_cast<int>(s.x), static_cast<int>(s.y)), 255, 0, 0, 8));
  // The particle (yellow quad) is drawn at its projected centre.
  const Vector3 q = Camera::main()->WorldToScreenPoint(Vector3(-4, 0, 0));
  const u8* pp = px(img, static_cast<int>(q.x), static_cast<int>(q.y));
  EXPECT(pp[0] > 200 && pp[1] > 200 && pp[2] < 80);
  // The blue line on layer 5 shows in the main view but not in the mini rectangle: its right half would cross x > 240, y > 136 there.
  const Vector3 b = Camera::main()->WorldToScreenPoint(Vector3(-2, -3, 0));
  EXPECT(isColor(px(img, static_cast<int>(b.x), static_cast<int>(b.y)), 0, 0, 255, 8));
  EXPECT(Particles::live() == 1);
  printf("fx-check %s quads=%u png=%s\n", g_fail ? "FAILED" : "ok", r.lastQuadCount(), out);
  r.shutdown();
  platform.shutdown();
  return g_fail ? 1 : 0;
}
