// PSP simulation-only ELF (F2.G2): loads host0:/game.rpak, optionally host0:/input.json, runs the real GameLoop for a fixed
// frame count with no rendering and logs "rensga: hash <hex>" so it can be compared with Linux rensga_headless.
// host0:/sim.cfg (optional, text): "frames=<N> trace=<0|1> objframe=<N>" (defaults: 400, 0, -1).
#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/platform/psp.h"
#include "rensga/Sim.h"
#include "runtime_boot.h"
#include "runtime_budget.h"

PSP_MODULE_INFO("rensga_sim", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

using namespace rensga;

int main(int, char** argv) {
  pspInstallLogSinks();
  Application::setPlatform(RuntimePlatform::Psp);
  RLOG_INFO("game", "rensga: boot");
  PspPlatform platform;
  platform.init();
  char root[128];
  pspProbeDataRoot(argv ? argv[0] : nullptr, "game.rpak", root, sizeof(root));
  PspFileSystem fs(root);

  // Arena from the pack's CTGT ramBytes (24 MB when the pack has none).
  PackConsoleTarget ctgt = {};
  const bool haveCtgt = peekPackTarget(fs, "data:/game.rpak", ctgt);
  const size_t ram = haveCtgt && ctgt.ramBytes ? ctgt.ramBytes : 24u * 1024 * 1024;
  Arena arena;
  if (!arena.create(arenaBytesForRam(ram, 0))) return 2;

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
  static BootState boot;  // no renderer: 2D components bind their records with null texture handles (tile collision works)
  BootDeps deps;
  deps.fs = &fs; deps.arena = &arena; deps.world = &world;
  deps.packPath = "data:/game.rpak";
  if (!bootPack(deps, boot)) {
    RLOG_ERROR("game", "rensga: cannot load %sgame.rpak", root);
    return 3;
  }
  logMemBudget(ram, arena);
  const u32 pickups = liveCount(world, "Pickup");
  RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", static_cast<unsigned>(world.objectCount()), static_cast<unsigned>(pickups));

  SimResult r = runSim(world, frames, haveInput ? &script : nullptr, pickups, &topt);
  heap::logAllocCheck();
  RLOG_INFO("game", "rensga: hash %08x", static_cast<unsigned>(r.hash));
  log::flushAll();
  world.shutdown();
  platform.shutdown();
  return 0;
}
