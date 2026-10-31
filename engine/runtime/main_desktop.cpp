// Desktop player (Linux, Windows, and the Android native library): SDL platform + GL renderer.
//   rensga_desktop [--pack <game.rpak>] [--scene <name>] [--frames N] [--screenshot out.png] [--stress-sprites N] [--check-at N] [--headless-gl]
// Without --pack it loads data:/game.rpak next to the executable (the APK assets on Android).
// --scene <name> start in this scene of the pack (default: the first). Saves go to the SDL pref folder (save:/).
// --frames N      stop after N frames (default: run until the window closes)
// --screenshot    write the virtual-resolution image of the last frame as a PNG (glReadPixels of the FBO)
// --stress-sprites N  no pack: N moving sprites (2D stress test), frame times in the heartbeat lines
// --headless-gl   accepted for scripts; run under xvfb-run, the player never needs a visible desktop
// Logs "rensga: frame <n>" every 60 frames and, on exit, "rensga: alloc-check frames=<N> allocs=<K>" (K must be 0;
// the exit code is 4 otherwise).
// --check-at N    also log the alloc-check line at frame N while running on (Android logs it at frame 600 and keeps going,
//                 because a phone app has no exit; tools/android/run_device_check.sh greps for it).
// Touch: SDL finger events drive the on-screen D-pad / South / East / Start zones (platform/touch.h); a translucent overlay
// shows them on Android always and elsewhere once a finger has touched the window.
// Hot reload (F2.03, desktop only, not Android/PS2): the pack file's mtime+size is polled twice a second; on a change the pack
// is read into the other of two reload buffers (sized at start), the current scene is reloaded by name, textures and audio
// clips are registered again and "rensga: hot-reload <pack>" is logged. This runs between frames, never inside one, and
// restarts the alloc-check window (heap::checkReset) because a reload is allowed to allocate.
// Audio (Engine 4): AudioMixer + SDL audio from the CTGT audio fields (runtime_audio.cpp). AudioSource clips come from the pack;
// data:/music.wav (next to the pack) streams as looping music. SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw captures it.
// --audio-stress   looping streamed music + 16 overlapping looping effects for --frames N (default 600); logs
//                  "rensga: audio-stress voices=16 mix_us=<avg>/<max> ..." (use with --pack, the pack folder is the data root)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define SDL_MAIN_HANDLED  // plain console main(); no SDL2main/WinMain needed
#endif
#include <SDL.h>  // on Android this renames main to SDL_main
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "rensga/Rensga.h"
#include "rensga/core/application.h"
#include "rensga/core/pack_watcher.h"
#include "rensga/platform/sdl.h"
#include "rensga/platform/sdl_filesystem.h"
#include "rensga/render/gl.h"
#include "profiler.h"
#include "runtime_audio.h"
#include "runtime_boot.h"
#include "runtime_prof_renderer.h"
#include "runtime_stress.h"

using namespace rensga;

#if defined(__ANDROID__)
class LogcatSink : public ILogSink {
 public:
  void write(const LogRecord& r) override {
    __android_log_print(r.level == LogLevel::Error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "rensga", "[%s] %s", r.channel, r.message);
  }
};
#endif

namespace {

constexpr u32 kMaxPackTextures = GfxAssets::kMaxTextures;

// ---- 2D stress test (runtime_stress.h) ---------------------------------------------------------------------
SpriteStress g_stress;
FrameProfiler g_prof;
bool g_stressOn = false;
double g_hudMs = 0;

// ---- helpers -----------------------------------------------------------------------------------------------
double nowMs() {
  return static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

// ---- touch overlay -----------------------------------------------------------------------------------------
SdlPlatform* g_platform = nullptr;

void drawTouchZones(IRenderer& r, const TouchZones& z) {  // translucent zones on top of everything
  const Color32 pad(255, 255, 255, 40), key(255, 255, 255, 90);
  const u32 k = 1000;
  r.drawRect(z.dpad, pad, k);
  const Scalar t = z.dpad.width / 3.0f;  // the d-pad's cross: four arms around the dead centre
  r.drawRect(Rect(z.dpad.x, z.dpad.y + t, t, t), key, k + 1);
  r.drawRect(Rect(z.dpad.x + 2 * t, z.dpad.y + t, t, t), key, k + 1);
  r.drawRect(Rect(z.dpad.x + t, z.dpad.y, t, t), key, k + 1);
  r.drawRect(Rect(z.dpad.x + t, z.dpad.y + 2 * t, t, t), key, k + 1);
  r.drawRect(z.south, key, k);
  r.drawRect(z.east, key, k);
  r.drawRect(z.start, pad, k);
}

void endHook(IRenderer& r, void*) {  // ProfilingRenderer::endFrame: after every other draw (Hud::Quad owns the global overlay hook)
  if (g_stressOn) g_stress.draw(r);  // the stress test has no touch controls
  else if (g_platform && g_platform->touchOverlayVisible()) drawTouchZones(r, g_platform->touchZones());
}

u64 audioClockUs() { return static_cast<u64>(nowMs() * 1000.0); }

void splitPath(const char* path, char* dir, size_t dirSize, const char** base) {
  const char* slash = strrchr(path, '/');
  const char* bslash = strrchr(path, '\\');
  if (bslash > slash) slash = bslash;
  if (!slash) { dir[0] = 0; *base = path; return; }
  size_t n = static_cast<size_t>(slash - path) + 1;
  if (n >= dirSize) n = dirSize - 1;
  memcpy(dir, path, n);
  dir[n] = 0;
  *base = slash + 1;
}

}  // namespace

int main(int argc, char** argv) {
  const char* packPath = nullptr;
  const char* shotPath = nullptr;
  const char* startScene = nullptr;
  u32 maxFrames = 0, stressCount = 0, checkAt = 0;
  bool audioStress = false, injectDemo = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--pack") && i + 1 < argc) packPath = argv[++i];
    else if (!strcmp(argv[i], "--scene") && i + 1 < argc) startScene = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shotPath = argv[++i];
    else if (!strcmp(argv[i], "--stress-sprites") && i + 1 < argc) stressCount = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--audio-stress")) audioStress = true;
    else if (!strcmp(argv[i], "--check-at") && i + 1 < argc) checkAt = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--headless-gl")) {}
    else if (!strcmp(argv[i], "--inject-demo")) injectDemo = true;  // F3.06: virtual pad + key events (InputTest screenshot)
    else {
      fprintf(stderr, "usage: rensga_desktop [--pack game.rpak] [--scene name] [--frames N] [--screenshot out.png] [--stress-sprites N] [--audio-stress] [--check-at N] [--headless-gl] [--inject-demo]\n");
      return 1;
    }
  }
#if defined(__ANDROID__)
  static LogcatSink logcat;
  log::addSink(&logcat);
  Application::setPlatform(RuntimePlatform::Android);
  if (!checkAt) checkAt = 600;  // log the alloc-check line once, then keep running
#elif defined(_WIN32)
  Application::setPlatform(RuntimePlatform::WindowsPlayer);
#else
  Application::setPlatform(RuntimePlatform::LinuxPlayer);
#endif
  RLOG_INFO("game", "rensga: boot");

  static SdlPlatform platform;
  static GlRenderer renderer;
  static RuntimeAudio audio;
  if (!platform.init()) return 2;
  g_platform = &platform;

  // File system: next to the pack when --pack is given, else the executable's folder (APK assets on Android).
  char dir[512] = "";
  const char* packFile = "data:/game.rpak";
  char packRel[300];
  if (packPath) {
    const char* base = nullptr;
    splitPath(packPath, dir, sizeof(dir), &base);
    snprintf(packRel, sizeof(packRel), "data:/%s", base);
    packFile = packRel;
  } else {
    char* bp = SDL_GetBasePath();
    if (bp) { snprintf(dir, sizeof(dir), "%s", bp); SDL_free(bp); }
  }
  static SdlFileSystem fs(dir);
  static FileSystemSaveStore saves(fs);

  Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes)) {
    RLOG_ERROR("game", "arena allocation failed");
    return 2;
  }
  World world;
  WorldConfig cfg;
  if (!world.init(cfg, arena)) return 2;

  u32 vw = 480, vh = 272;  // PSP profile resolution unless the pack says otherwise
  static BootState boot;   // BootState must not move: SceneManager and GfxAssets point into it
  PackView& view = boot.view;
  LoadedPack& loaded = boot.loaded;
  const PackConsoleTarget* ct = nullptr;
  static u32 packTex[kMaxPackTextures] = {};
  if (stressCount) {
    if (!renderer.init(vw, vh)) return 3;
    platform.setVirtualResolution(vw, vh);
    if (!g_stress.setup(renderer, stressCount, vw, vh, false)) { RLOG_ERROR("game", "stress setup failed"); return 3; }
    g_stressOn = true;
    RLOG_INFO("game", "rensga: stress sprites=%u", g_stress.count());
  } else {
    BootDeps deps;
    deps.fs = &fs; deps.arena = &arena; deps.world = &world;
    deps.packPath = packFile; deps.startScene = startScene;
    deps.renderer = &renderer;
    deps.textureHandles = packTex; deps.textureHandleCapacity = kMaxPackTextures;
    deps.defaultWidth = vw; deps.defaultHeight = vh;
    deps.onResolution = [](void* user, u32 w, u32 h) { static_cast<SdlPlatform*>(user)->setVirtualResolution(w, h); };
    deps.onResolutionUser = &platform;
    deps.saves = &saves;
    deps.audio = &audio; deps.audioClock = audioClockUs;
    if (!bootPack(deps, boot)) return 3;
    vw = boot.width; vh = boot.height;
    ct = boot.target;
    u32 pickups = 0;
    world.forEach<MeshRenderer>([&](MeshRenderer& mr) {
      GameObject* go = mr.gameObject();
      if (go && go->name[0] == 'P' && go->name[1] == 'i' && go->name[2] == 'c') ++pickups;
    });
    RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", world.objectCount(), pickups);
  }

  if (audioStress && maxFrames == 0) maxFrames = 600;
  if (stressCount) audio.init(&fs, ct, audioClockUs);  // with a pack, bootPack already initialised the audio and registered the clips
  if (audioStress) audio.beginStress(stressCount ? nullptr : &loaded.assets);
  else if (!stressCount) audio.startMusic();

#if defined(RENSGA_HAS_PACK_WATCHER) && !defined(__ANDROID__)
  // Hot reload buffers (F2.03): two arenas sized from the pack, allocated here once. A reload reads into the one that is
  // not in use; the world's own arena is untouched (pools keep their capacity).
  PackWatcher watcher;
  Arena reloadArena[2];
  u32 reloadCur = 0;  // arena holding the live pack after a reload (the first pack lives in `arena`)
  bool reloadLive = false;
  double nextWatchMs = 0;
  char watchPath[600] = "";
  if (!stressCount) {
    snprintf(watchPath, sizeof(watchPath), "%s%s", dir, packFile + 6);  // skip "data:/"
    watcher.watch(watchPath);
    const size_t cap = static_cast<size_t>(view.size) * 2 + (1u << 20);
    if (!reloadArena[0].create(cap) || !reloadArena[1].create(cap)) RLOG_WARN("game", "rensga: hot reload disabled (no memory)");
  }
  auto hotReload = [&]() {
    Arena& next = reloadArena[reloadLive ? 1 - reloadCur : 0];
    if (!next.capacity()) return;
    next.reset();
    Span<const u8> bytes = fs.readFile(packFile, next);
    PackView nv;
    if (!bytes || !nv.open(bytes.data, bytes.size, watchPath)) {
      RLOG_WARN("game", "rensga: hot-reload failed: cannot read %s (keeping the running pack)", watchPath);
      return;
    }
    char sceneName[64];
    snprintf(sceneName, sizeof(sceneName), "%s", SceneManager::activeSceneName());
    audio.mixer().stopAll();  // voices may point into the old pack buffer
    world.destroyAllImmediate();
    Gameplay2D::resetTriggerMemory();
    UI::resetFocus();
    view = nv;  // the object the SceneManager / GfxAssets point at, now on the new buffer
    SceneManager::bind(&view, 0);
    i32 si = sceneName[0] ? SceneManager::sceneIndexByName(sceneName) : 0;
    if (si < 0) si = 0;
    loaded = LoadedPack();
    if (!world.loadPack(view, &loaded, static_cast<u32>(si))) {
      RLOG_ERROR("game", "rensga: hot-reload: cannot load the new pack %s", watchPath);
      return;
    }
    reloadCur = reloadLive ? 1 - reloadCur : 0;
    reloadLive = true;
    Input::loadActions(view, loaded.inputs, loaded.inputCount);
    renderer.setMeshes(loaded.meshes);
    renderer.releaseUserTextures();
    boot.textureCount = uploadPackTextures(renderer, loaded.assets, packTex, kMaxPackTextures);
    GfxAssets::bind(&view, &loaded.assets, packTex, boot.textureCount);
    SceneManager::bind(&view, static_cast<u32>(si));
    audio.registerPackClips(loaded.assets);
    heap::checkReset();  // the reload may allocate; the check window restarts
    RLOG_INFO("game", "rensga: hot-reload %s (scene %s, objects=%u)", watchPath, SceneManager::activeSceneName(), world.objectCount());
    fflush(stdout);
  };
#endif

  g_prof.init(audioClockUs);
  static ProfilingRenderer profRenderer(renderer, g_prof);
  profRenderer.setEndHook(endHook);
  GameLoop loop;
  loop.init(world);
  Input::bindPlatform(&platform);  // rumble requests leave the loop through IPlatform::setVibration
  loop.setFixedStepHook(physics::fixedStep, nullptr);

  double last = platform.timeSeconds();
  VirtualGamepad pad;
  u32 frame = 0;
  double sumMs = 0, maxMs = 0;
  while (!platform.shouldQuit() && !Application::quitRequested() && (maxFrames == 0 || frame < maxFrames)) {
    const double t0 = nowMs();
    g_prof.frameBegin();
    const double now = platform.timeSeconds();
#if defined(RENSGA_HAS_PACK_WATCHER) && !defined(__ANDROID__)
    if (watcher.watching() && t0 >= nextWatchMs) {  // ~2 polls a second, between frames
      nextWatchMs = t0 + 500.0;
      if (watcher.check()) hotReload();
    }
#endif
    if (injectDemo) platform.injectDemo(frame);
    platform.pollInput(pad);
    loop.frame(static_cast<Scalar>(now - last), pad);
    last = now;
    g_prof.mark(ProfSection::Update);
    if (stressCount) {
      g_stress.step(vw, vh);
      g_stress.setFrameMs(static_cast<float>(g_hudMs));
    }
    renderWorld(world, profRenderer, stressCount ? Color32(28, 26, 22, 255) : Color32(40, 60, 90, 255));
    g_prof.mark(ProfSection::Present);  // the Render mark was taken in ProfilingRenderer::endFrame, just before the swap
    audio.update();
    g_prof.mark(ProfSection::Audio);
    g_prof.frameEnd();
    ++frame;
    // The first frame includes lazy driver work (shader JIT, buffer setup): the alloc check starts after it.
    if (frame == 1) heap::checkReset();
    if (checkAt && frame == checkAt) heap::logAllocCheck();
    const double ms = nowMs() - t0;
    sumMs += ms;
    if (ms > maxMs) maxMs = ms;
    g_hudMs = ms;
    if (frame % 60 == 0) {
      RLOG_INFO("game", "rensga: frame %u avg=%.2fms max=%.2fms sprites=%u draws=%u", frame, sumMs / 60.0, maxMs,
                renderer.lastSpriteCount(), renderer.lastDrawCalls());
      sumMs = maxMs = 0;
      fflush(stdout);
    }
  }
  if (audioStress) audio.logStress(frame);
  const u64 allocs = heap::logAllocCheck();  // before the screenshot: the PNG writer is allowed to allocate
  int rc = 0;
  if (shotPath && !renderer.screenshot(shotPath)) rc = 5;
  else if (shotPath) RLOG_INFO("game", "rensga: screenshot %s", shotPath);
  if (stressCount && renderer.droppedSprites()) RLOG_WARN("game", "rensga: dropped %u sprites", renderer.droppedSprites());
  RLOG_INFO("game", "rensga: quit after %u frames", frame);
  log::flushAll();
  audio.shutdown();
  world.shutdown();
  renderer.shutdown();  // while the GL context exists; the static's destructor runs after it is gone
  platform.shutdown();
  if (allocs != 0 && rc == 0) rc = 4;
  return rc;
}
