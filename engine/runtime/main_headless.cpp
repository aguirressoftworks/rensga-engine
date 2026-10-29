// Foundation stub of the linux-host simulation. Usage: rensga_headless [game.rpak] [frames]
#include <stdio.h>
#include <stdlib.h>

#include "rensga/Rensga.h"

using namespace rensga;

int main(int argc, char** argv) {
  const char* packPath = argc > 1 ? argv[1] : nullptr;
  u32 frames = argc > 2 ? static_cast<u32>(atoi(argv[2])) : 120;
  RLOG_INFO("game", "rensga: boot");

  Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes)) return 2;
  World world;
  WorldConfig cfg;
  if (!world.init(cfg, arena)) return 2;

  u32 pickups = 0;
  if (packPath) {
    StdioFileSystem fs;
    Span<const u8> bytes = fs.readFile(packPath, arena);
    PackView view;
    LoadedPack loaded;
    if (!bytes || !view.open(bytes.data, bytes.size) || !world.loadPack(view, &loaded)) return 3;
    Input::loadActions(view, loaded.inputs, loaded.inputCount);
  }
  RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", world.objectCount(), pickups);

  NullPlatform platform;
  platform.maxPolls = frames;
  GameLoop loop;
  loop.init(world);
  loop.setFixedStepHook(physics::fixedStep, nullptr);
  loop.run(platform, nullptr);
  world.shutdown();
  return 0;
}
