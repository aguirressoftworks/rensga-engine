// PSP entry point (PSP Engine 1/4, F4.03/F4.06): data root probe -> arena from the pack's CTGT -> bootPack (runtime_boot.h) ->
// fixed-timestep loop -> GU render. SDL2 serves the game controller and the audio device only (platform/psp, runtime_audio.cpp);
// the module info, the exit/power callbacks, the clock and main() are ours.
//
// Build options (engine/runtime/CMakeLists.txt): RENSGA_EXIT_AFTER_FRAMES=<n> exit + frame dump (CI), RENSGA_PSP_STRESS_SPRITES=<n>,
// RENSGA_PSP_AUDIO_STRESS=ON, RENSGA_PSP_AUTOSTART_SCENE=<name>, RENSGA_PSP_CPU_MHZ=222|333 (platform/psp).
// Logs: "rensga: data root=", "rensga: mem budget=", "rensga: prof ..." every 60 frames, "rensga: alloc-check ...".
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <stdio.h>

#include "rensga/Rensga.h"
#include "rensga/platform/psp.h"
#include "rensga/render/gu.h"
#include "profiler.h"
#include "runtime_audio.h"
#include "runtime_boot.h"
#include "runtime_budget.h"
#include "runtime_prof_renderer.h"
#include "runtime_stress.h"

PSP_MODULE_INFO("rensga", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);  // everything except 1 MB for the system

#ifndef RENSGA_PSP_STRESS_SPRITES
#define RENSGA_PSP_STRESS_SPRITES 0
#endif

using namespace rensga;

namespace {

// Built-in fallback: a unit cube with per-face colours (36 vertices, GU layout).
alignas(16) PackVertex g_cubeVerts[36];
alignas(16) PackMeshRec g_cubeMesh[1];

void buildTestCube() {
  static const float c[8][3] = {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
                                {-.5f, -.5f, .5f},  {.5f, -.5f, .5f},  {.5f, .5f, .5f},  {-.5f, .5f, .5f}};
  static const int f[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
  static const Color32 col[6] = {{220, 60, 60}, {60, 220, 60}, {60, 60, 220}, {220, 220, 60}, {60, 220, 220}, {220, 60, 220}};
  int n = 0;
  for (int i = 0; i < 6; ++i) {
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; ++k) {
      const float* p = c[f[i][idx[k]]];
      g_cubeVerts[n++] = PackVertex{col[i].packedABGR(), p[0], p[1], p[2]};
    }
  }
  g_cubeMesh[0] = PackMeshRec{0, 36, 0, 0};
}

u64 clockUs() { return static_cast<u64>(sceKernelGetSystemTimeWide()); }

FrameProfiler g_prof;
#if RENSGA_PSP_STRESS_SPRITES > 0
SpriteStress g_stress;
#endif

// Runs inside ProfilingRenderer::endFrame, after every other draw (Hud::Quad owns the global overlay hook, so this is not one).
void endHook(IRenderer& r, void*) {
#if RENSGA_PSP_STRESS_SPRITES > 0
  g_stress.draw(r);
#else
  (void)r;
#endif
}

// Arena: the CTGT budget, or the largest block the heap can give when the device has less (PSP-1000: 24 MB for the game).
bool createArena(Arena& arena, size_t ram) {
  size_t want = arenaBytesForRam(ram, 0);
  for (int tries = 0; tries < 8; ++tries) {
    if (arena.create(want)) {
      if (tries) RLOG_WARN("game", "rensga: mem arena reduced to %u bytes (the heap could not give %u)", static_cast<unsigned>(want),
                           static_cast<unsigned>(arenaBytesForRam(ram, 0)));
      return true;
    }
    want -= 1024 * 1024;
  }
  return false;
}

}  // namespace

int main(int, char** argv) {
  pspInstallLogSinks();
  Application::setPlatform(RuntimePlatform::Psp);
  RLOG_INFO("game", "rensga: boot");

  static PspPlatform platform;
  static GuRenderer renderer;
  static RuntimeAudio audio;
  platform.init();

  // Data root: argv[0]'s folder, ms0:, umd0:, host0: (logged), then the arena from the pack's own RAM budget.
  char root[128];
  pspProbeDataRoot(argv ? argv[0] : nullptr, "game.rpak", root, sizeof(root));
  static PspFileSystem fs(root);
  static FileSystemSaveStore saves(fs);
  const char* packPath = "data:/game.rpak";
  PackConsoleTarget ctgt = {};
  const bool haveCtgt = peekPackTarget(fs, packPath, ctgt);
  const size_t ram = haveCtgt && ctgt.ramBytes ? ctgt.ramBytes : 24u * 1024 * 1024;
  RLOG_INFO("game", "rensga: psp mem total_free=%u max_free=%u ctgt_ram=%u", static_cast<unsigned>(sceKernelTotalFreeMemSize()),
            static_cast<unsigned>(sceKernelMaxFreeMemSize()), static_cast<unsigned>(ram));

  static Arena arena;
  if (!createArena(arena, ram)) {
    RLOG_ERROR("game", "arena allocation failed");
    platform.shutdown();
    return 1;
  }
  WorldConfig cfg;
  static World world;
  if (!world.init(cfg, arena)) {
    RLOG_ERROR("game", "world init failed");
    platform.shutdown();
    return 1;
  }
  if (!renderer.init(480, 272)) {
    RLOG_ERROR("game", "renderer init failed");
    platform.shutdown();
    return 1;
  }

  static BootState boot;
  static u32 packTex[GfxAssets::kMaxTextures] = {};
  BootDeps deps;
  deps.fs = &fs; deps.arena = &arena; deps.world = &world;
  deps.packPath = packPath;
  deps.renderer = &renderer; deps.initRenderer = false;
  deps.textureHandles = packTex; deps.textureHandleCapacity = GfxAssets::kMaxTextures;
  deps.onResolution = [](void* user, u32 w, u32 h) { static_cast<GuRenderer*>(user)->setVirtualResolution(w, h); };
  deps.onResolutionUser = &renderer;
  deps.saves = &saves;
  deps.audio = &audio; deps.audioClock = clockUs;
  const bool havePack = bootPack(deps, boot);
  if (havePack) {
    Input::bindPlatform(&platform);
  } else {
    RLOG_WARN("game", "no usable %s%s, drawing the built-in test cube", root, "game.rpak");
    buildTestCube();
    MeshTable t;
    t.meshes = g_cubeMesh;
    t.meshCount = 1;
    t.vertices = g_cubeVerts;
    t.vertexCount = 36;
    renderer.setMeshes(t);
    GameObject* cam = world.createObject("Camera");
    cam->AddComponent<Camera>();
    cam->transform.lpos_ = Vector3(0, 1.2f, 3.5f);
    cam->transform.lrot_ = Quaternion::AngleAxis(-15.0f, Vector3(1, 0, 0));
    GameObject* cube = world.createObject("Cube");
    cube->AddComponent<MeshRenderer>()->meshIndex = 0;
    Save::use(&saves);
    audio.init(&fs, nullptr, clockUs);
  }
  u32 pickups = 0;
  world.forEach<MeshRenderer>([&](MeshRenderer& mr) {
    GameObject* go = mr.gameObject();
    if (go && go->name[0] == 'P' && go->name[1] == 'i' && go->name[2] == 'c') ++pickups;
  });
  RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", world.objectCount(), pickups);
  logMemBudget(ram, arena);

#if RENSGA_PSP_STRESS_SPRITES > 0
  if (!g_stress.setup(renderer, RENSGA_PSP_STRESS_SPRITES, 480, 272, true)) RLOG_ERROR("game", "rensga: stress setup failed");
  RLOG_INFO("game", "rensga: stress sprites=%u", g_stress.count());
#endif
#if defined(RENSGA_PSP_AUDIO_STRESS)
  audio.beginStress(havePack ? &boot.loaded.assets : nullptr);  // 16 looping effects + streamed music; logs mix_us at the end
#else
  if (havePack) audio.startMusic();  // data:/music.wav next to game.rpak; absent is not an error
#endif

  g_prof.init(clockUs);
  static ProfilingRenderer profRenderer(renderer, g_prof);
  profRenderer.setEndHook(endHook);
  GameLoop loop;
  loop.init(world);
  loop.setFixedStepHook(physics::fixedStep, nullptr);

#if defined(RENSGA_PSP_AUDIO_STRESS) && !defined(RENSGA_EXIT_AFTER_FRAMES)
#define RENSGA_EXIT_AFTER_FRAMES 600
#endif

  double last = platform.timeSeconds();
  VirtualGamepad pad;
  u32 frame = 0, lastResumes = PspPlatform::resumeCount(), lastSuspends = PspPlatform::suspendCount();
  while (!platform.shouldQuit() && !Application::quitRequested()) {
    g_prof.frameBegin();
    if (PspPlatform::suspendCount() != lastSuspends || PspPlatform::resumeCount() != lastResumes) {  // set by the power callback thread
      lastSuspends = PspPlatform::suspendCount();
      lastResumes = PspPlatform::resumeCount();
      RLOG_INFO("game", "rensga: power suspends=%u resumes=%u (frame %u)", lastSuspends, lastResumes, static_cast<unsigned>(frame));
      last = platform.timeSeconds();  // the sleep must not become one huge frame step
    }
    double now = platform.timeSeconds();
    platform.pollInput(pad);
    loop.frame(static_cast<Scalar>(now - last), pad);
    last = now;
    if (!havePack) {
      GameObject* cube = world.findByName("Cube");
      if (cube) cube->transform.lrot_ = Quaternion::Euler(20.0f, static_cast<Scalar>(frame) * 1.5f, 0);
    }
#if RENSGA_PSP_STRESS_SPRITES > 0
    g_stress.step(480, 272);
#endif
    g_prof.mark(ProfSection::Update);
    renderWorld(world, profRenderer);  // the Render mark is taken in ProfilingRenderer::endFrame, just before the swap
    g_prof.mark(ProfSection::Present);
    audio.update();
    g_prof.mark(ProfSection::Audio);
    g_prof.frameEnd();
    ++frame;
    if (frame == 1) heap::checkReset();  // first frame includes lazy GU/driver setup (same rule as the desktop player)
#ifdef RENSGA_PSP_AUTOSTART_SCENE
    if (havePack && frame == 30) SceneManager::LoadScene(RENSGA_PSP_AUTOSTART_SCENE);
#endif
#ifdef RENSGA_EXIT_AFTER_FRAMES
    if (frame >= RENSGA_EXIT_AFTER_FRAMES) break;  // screenshot/CI builds (PPSSPPHeadless needs a clean exit)
#endif
    if (frame == 600) heap::logAllocCheck();  // dev builds run on until the editor stops them: log the alloc-check once (like the desktop player)
    if (frame % 60 == 0) RLOG_INFO("game", "rensga: frame %u", static_cast<unsigned>(frame));
  }
  renderer.flush();  // the GE must be idle before VRAM is read or the program exits
#if defined(RENSGA_PSP_AUDIO_STRESS)
  audio.logStress(frame);
#endif
  heap::logAllocCheck();
#ifdef RENSGA_EXIT_AFTER_FRAMES
  renderer.dumpFrame();  // host0:/rensga_shot.bmp, read from the 5650 draw buffer by the GU backend
#endif
  logMemBudget(ram, arena);
  RLOG_INFO("game", "rensga: quit after %u frames", static_cast<unsigned>(frame));
  log::flushAll();
  audio.shutdown();
  world.shutdown();
  platform.shutdown();
  return 0;
}
