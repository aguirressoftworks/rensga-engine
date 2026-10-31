// F5.14 GL: plays a cooked 3D pack (scene by name, N fixed frames) straight through GlRenderer (no profiler wrapper, no
// scripts) and writes the last frame as a PNG. Usage: room_shot <game.rpak> <scene> <frames> <out.png>
#include <stdio.h>
#include <stdlib.h>

#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/scene_manager.h"
#include "rensga/core/world.h"
#include "rensga/platform/sdl.h"
#include "rensga/render/gl.h"

using namespace rensga;

int main(int argc, char** argv) {
  if (argc < 5) return 1;
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  u8* buf = static_cast<u8*>(aligned_alloc(16, (static_cast<size_t>(n) + 15) & ~size_t(15)));
  if (fread(buf, 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) return 2;
  fclose(f);
  static PackView view;
  if (!view.open(buf, static_cast<size_t>(n), argv[1])) return 2;
  Arena arena;
  arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes);
  static World world;
  WorldConfig wc;
  if (!world.init(wc, arena)) return 2;
  SceneManager::bind(&view, 0);
  const i32 si = SceneManager::sceneIndexByName(argv[2]);
  static LoadedPack lp;
  if (!world.loadPack(view, &lp, si < 0 ? 0 : static_cast<u32>(si))) return 2;
  SceneManager::bind(&view, si < 0 ? 0 : static_cast<u32>(si));

  static SdlPlatform platform;
  static GlRenderer r;
  if (!platform.init() || !r.init(480, 272)) return 3;
  r.setMeshes(lp.meshes);
  static u32 tex[GfxAssets::kMaxTextures];
  for (u32 i = 0; i < lp.assets.textureCount && i < GfxAssets::kMaxTextures; ++i) {
    const PackTexRec& t = lp.assets.textures[i];
    TextureDesc d;
    d.width = t.width; d.height = t.height; d.format = static_cast<u8>(t.format);
    d.pixels = lp.assets.texturePixels(t); d.palette = lp.assets.texturePalette(t);
    d.storedWidth = t.storedWidth; d.storedHeight = t.storedHeight; d.flags = t.flags;
    tex[i] = r.uploadTexture(d);
  }
  GfxAssets::bind(&view, &lp.assets, tex, lp.assets.textureCount);
  GameLoop loop;
  loop.init(world);
  // Optional camera override: ROOM_CAM="ex,ey,ez,tx,ty,tz" (eye, target).
  if (const char* cam = getenv("ROOM_CAM")) {
    float v[6] = {};
    if (sscanf(cam, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
      world.forEach<Camera>([&](Camera& c) {
        c.transform()->setPosition(Vector3(v[0], v[1], v[2]));
        c.transform()->setRotation(Quaternion::LookRotation(Vector3(v[0] - v[3], v[1] - v[4], v[2] - v[5]))  /* the view looks down local -Z */);
      });
  }
  const u32 frames = static_cast<u32>(atoi(argv[3]));
  VirtualGamepad pad;
  for (u32 i = 0; i < frames; ++i) {
    loop.frame(1.0f / 60.0f, pad);
    renderWorld(world, r, Color32(40, 60, 90, 255));
  }
  const bool ok = r.screenshot(argv[4]);
  printf("room-shot %s scene=%s frames=%u draws=%u\n", ok ? "ok" : "FAILED", argv[2], frames, r.lastDrawCalls());
  r.shutdown();
  platform.shutdown();
  return ok ? 0 : 4;
}
