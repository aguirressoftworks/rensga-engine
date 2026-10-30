// Linux-host simulation: the real GameLoop with null backends and scripted VirtualGamepad input.
//   rensga_headless (--pack <game.rpak> | --scene-test) [--frames N] [--input script.json] [--save-dir D] [--hash] [--trace] [--obj-frame N]
// --save-dir D  where save:/ points (default: the current folder); 2D packs bind their assets with null texture handles.
// Prints the acceptance log lines, "rensga: alloc-check frames=<N> allocs=<K>" and, with --hash, "rensga: hash <hex>".
// --trace logs one hash per frame; --obj-frame N dumps every object's bits after frame N (see Sim.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/Sim.h"

using namespace rensga;

static void usage() {
  fprintf(stderr, "usage: rensga_headless (--pack <file> | --scene-test) [--frames N] [--input script.json] [--save-dir D] [--hash] [--trace] [--obj-frame N]\n");
}

int main(int argc, char** argv) {
  const char* packPath = nullptr;
  const char* inputPath = nullptr;
  const char* saveDir = nullptr;
  bool sceneTest = false, printHash = false;
  u32 frames = 120;
  SimTraceOptions topt;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--pack") && i + 1 < argc) packPath = argv[++i];
    else if (!strcmp(argv[i], "--input") && i + 1 < argc) inputPath = argv[++i];
    else if (!strcmp(argv[i], "--save-dir") && i + 1 < argc) saveDir = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--scene-test")) sceneTest = true;
    else if (!strcmp(argv[i], "--hash")) printHash = true;
    else if (!strcmp(argv[i], "--trace")) topt.trace = true;
    else if (!strcmp(argv[i], "--obj-frame") && i + 1 < argc) topt.objFrame = atoll(argv[++i]);
    else { usage(); return 1; }
  }
  if (!packPath && !sceneTest) { usage(); return 1; }

  Application::setPlatform(RuntimePlatform::Headless);
  RLOG_INFO("game", "rensga: boot");
  Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes)) return 2;
  char saveRoot[300] = "";
  if (saveDir) snprintf(saveRoot, sizeof(saveRoot), "%s/", saveDir);
  StdioFileSystem fs("", nullptr, saveDir ? saveRoot : nullptr);
  FileSystemSaveStore saves(fs);
  Save::use(&saves);

  InputScript script;
  if (inputPath) {
    Span<const u8> bytes = fs.readFile(inputPath, arena);
    if (!bytes || !script.parse(reinterpret_cast<const char*>(bytes.data), bytes.size)) {
      RLOG_ERROR("game", "rensga: cannot read input script %s", inputPath);
      return 3;
    }
  }

  World world;
  WorldConfig cfg;
  if (!world.init(cfg, arena)) return 2;

  if (packPath) {
    Span<const u8> bytes = fs.readFile(packPath, arena);
    static PackView view;
    LoadedPack loaded;
    static u32 noTextures[GfxAssets::kMaxTextures] = {};  // headless: no renderer, so every handle is 0
    if (!bytes || !view.open(bytes.data, bytes.size, packPath) || !world.loadPack(view, &loaded)) {
      RLOG_ERROR("game", "rensga: cannot load pack %s", packPath);
      return 3;
    }
    Input::loadActions(view, loaded.inputs, loaded.inputCount);
    GfxAssets::bind(&view, &loaded.assets, noTextures, loaded.assets.textureCount);
    SceneManager::bind(&view, 0);
  } else {
    defineDefaultInput();
    if (!buildTestLevel(world)) { RLOG_ERROR("game", "rensga: test level needs the project scripts"); return 3; }
  }
  const u32 pickups = liveCount(world, "Pickup");
  RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", world.objectCount(), pickups);

  SimResult r = runSim(world, frames, inputPath ? &script : nullptr, pickups, &topt);
  const u64 allocs = heap::logAllocCheck();
  if (printHash) printf("rensga: hash %08x\n", r.hash);
  world.shutdown();
  return allocs == 0 ? 0 : 4;
}
