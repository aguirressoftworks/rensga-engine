// Linux-host simulation: the real GameLoop with null backends and scripted VirtualGamepad input.
//   rensga_headless (--pack <game.rpak> | --scene-test) [--frames N] [--input script.json] [--hash]
// Prints the acceptance log lines and, with --hash, "rensga: state hash <hex>".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/Sim.h"

using namespace rensga;

static void usage() {
  fprintf(stderr, "usage: rensga_headless (--pack <file> | --scene-test) [--frames N] [--input script.json] [--hash]\n");
}

int main(int argc, char** argv) {
  const char* packPath = nullptr;
  const char* inputPath = nullptr;
  bool sceneTest = false, printHash = false;
  u32 frames = 120;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--pack") && i + 1 < argc) packPath = argv[++i];
    else if (!strcmp(argv[i], "--input") && i + 1 < argc) inputPath = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--scene-test")) sceneTest = true;
    else if (!strcmp(argv[i], "--hash")) printHash = true;
    else { usage(); return 1; }
  }
  if (!packPath && !sceneTest) { usage(); return 1; }

  RLOG_INFO("game", "rensga: boot");
  Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes)) return 2;
  StdioFileSystem fs;

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
    PackView view;
    LoadedPack loaded;
    if (!bytes || !view.open(bytes.data, bytes.size) || !world.loadPack(view, &loaded)) {
      RLOG_ERROR("game", "rensga: cannot load pack %s", packPath);
      return 3;
    }
    Input::loadActions(view, loaded.inputs, loaded.inputCount);
  } else {
    defineDefaultInput();
    if (!buildTestLevel(world)) { RLOG_ERROR("game", "rensga: test level needs the project scripts"); return 3; }
  }
  const u32 pickups = liveCount(world, "Pickup");
  RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", world.objectCount(), pickups);

  SimResult r = runSim(world, frames, inputPath ? &script : nullptr, pickups);
  if (printHash) printf("rensga: state hash %08x\n", r.hash);
  world.shutdown();
  return 0;
}
