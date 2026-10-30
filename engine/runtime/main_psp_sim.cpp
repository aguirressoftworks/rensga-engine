// PSP simulation-only ELF (F2.G2): loads host0:/game.rpak, optionally host0:/input.json, runs the real GameLoop for a fixed
// frame count with no rendering and logs "rensga: hash <hex>" so it can be compared with Linux rensga_headless.
// host0:/sim.cfg (optional, text): "frames=<N> trace=<0|1> objframe=<N>" (defaults: 400, 0, -1).
#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/platform/psp.h"
#include "sim_trace.h"

PSP_MODULE_INFO("rensga_sim", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

using namespace rensga;

int main() {
  pspInstallLogSinks();
  RLOG_INFO("game", "rensga: boot");
  PspPlatform platform;
  PspFileSystem fs;
  platform.init();

  Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes)) return 2;

  u32 frames = 400;
  SimTraceOptions topt;
  Span<const u8> cfg = fs.readFile("sim.cfg", arena);
  if (cfg) {
    char buf[128];
    size_t n = cfg.size < sizeof(buf) - 1 ? cfg.size : sizeof(buf) - 1;
    memcpy(buf, cfg.data, n);
    buf[n] = 0;
    if (const char* p = strstr(buf, "frames=")) frames = static_cast<u32>(atoi(p + 7));
    if (const char* p = strstr(buf, "trace=")) topt.trace = atoi(p + 6) != 0;
    if (const char* p = strstr(buf, "objframe=")) topt.objFrame = atoi(p + 9);
  }

  InputScript script;
  bool haveInput = false;
  Span<const u8> in = fs.readFile("input.json", arena);
  if (in) {
    if (!script.parse(reinterpret_cast<const char*>(in.data), in.size)) { RLOG_ERROR("game", "rensga: cannot parse input.json"); return 3; }
    haveInput = true;
  }

  World world;
  WorldConfig wcfg;
  if (!world.init(wcfg, arena)) return 2;
  Span<const u8> bytes = fs.readFile("game.rpak", arena);
  PackView view;
  LoadedPack loaded;
  if (!bytes || !view.open(bytes.data, bytes.size) || !world.loadPack(view, &loaded)) {
    RLOG_ERROR("game", "rensga: cannot load host0:/game.rpak");
    return 3;
  }
  Input::loadActions(view, loaded.inputs, loaded.inputCount);
  const u32 pickups = liveCount(world, "Pickup");
  RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", static_cast<unsigned>(world.objectCount()), static_cast<unsigned>(pickups));

  SimResult r = runSimTraced(world, frames, haveInput ? &script : nullptr, pickups, topt);
  heap::logAllocCheck();
  RLOG_INFO("game", "rensga: hash %08x", static_cast<unsigned>(r.hash));
  log::flushAll();
  world.shutdown();
  platform.shutdown();
  return 0;
}
