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
#include "runtime_audio.h"

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

constexpr u32 kMaxStress = 16000;
constexpr u32 kMaxPackTextures = 64;

// ---- 2D stress test -----------------------------------------------------------------------------------------
struct Sprite {
  float x, y, vx, vy;
  u32 tex;  // 0 or 1
  u8 r, g, b;
};
Sprite g_sprites[kMaxStress];
u32 g_spriteCount = 0;
u32 g_ballTex = 0, g_boxTex = 0, g_fontTex = 0;
PackFontRec g_font;
char g_hud[32] = "0";
float g_hudMs = 0;

u32 g_seed = 12345;
float rnd() {  // deterministic LCG, 0..1
  g_seed = g_seed * 1664525u + 1013904223u;
  return static_cast<float>((g_seed >> 8) & 0xFFFF) / 65536.0f;
}

// 16x16 ball as a CLUT4 image: 0 transparent, 1 rim, 2 body, 3 highlight.
void makeBall(u8* packed, u32* pal) {
  memset(packed, 0, 16 * 8);
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      const float dx = x - 7.5f, dy = y - 7.5f, d2 = dx * dx + dy * dy;
      u8 v = 0;
      if (d2 <= 56.0f) v = d2 > 40.0f ? 1 : ((dx + 3) * (dx + 3) + (dy + 3) * (dy + 3) < 6.0f ? 3 : 2);
      u8& b = packed[y * 8 + x / 2];
      b = static_cast<u8>((x & 1) ? (b | (v << 4)) : (b | v));
    }
  for (int i = 0; i < 16; ++i) pal[i] = 0;
  pal[1] = 0xFF404040u;  // R in the low byte
  pal[2] = 0xFFB0B0B0u;
  pal[3] = 0xFFFFFFFFu;
}

// 16x16 box as CLUT8: a gradient frame, index = distance from the edge.
void makeBox(u8* idx, u32* pal) {
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      const int e = x < y ? x : y, f = (15 - x) < (15 - y) ? (15 - x) : (15 - y);
      const int m = e < f ? e : f;
      idx[y * 16 + x] = static_cast<u8>(m > 3 ? 4 : m);
    }
  for (int i = 0; i < 256; ++i) pal[i] = 0;
  pal[0] = 0xFF303030u; pal[1] = 0xFF808080u; pal[2] = 0xFFB0B0B0u; pal[3] = 0xFFE0E0E0u; pal[4] = 0xFFFFFFFFu;
}

// 3x5 digit font in 4x6 cells (CLUT4, 40 cells in one row of 10 per image row).
void makeDigitFont(u8* packed, u32* pal) {
  static const char* kDigits[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111",
                                    "101101111001001", "111100111001111", "111100111101111", "111001001001001",
                                    "111101111101111", "111101111001111"};
  memset(packed, 0, 20 * 6);  // 40 px wide = 20 bytes per row, 6 rows
  for (int d = 0; d < 10; ++d)
    for (int y = 0; y < 5; ++y)
      for (int x = 0; x < 3; ++x)
        if (kDigits[d][y * 3 + x] == '1') {
          const int px = d * 4 + x;
          u8& b = packed[y * 20 + px / 2];
          b = static_cast<u8>((px & 1) ? (b | 0x10) : (b | 0x01));
        }
  for (int i = 0; i < 16; ++i) pal[i] = 0;
  pal[1] = 0xFFFFFFFFu;
}

bool setupStress(IRenderer& r, u32 count, u32 w, u32 h) {
  static u8 ball[16 * 8], box[16 * 16], digits[20 * 6];
  static u32 ballPal[16], boxPal[256], digitPal[16];
  makeBall(ball, ballPal);
  makeBox(box, boxPal);
  makeDigitFont(digits, digitPal);
  TextureDesc d;
  d.width = d.height = 16;
  d.format = tex_format::CLUT4; d.pixels = ball; d.palette = ballPal;
  g_ballTex = r.uploadTexture(d);
  d.format = tex_format::CLUT8; d.pixels = box; d.palette = boxPal;
  g_boxTex = r.uploadTexture(d);
  d.width = 40; d.height = 6;
  d.format = tex_format::CLUT4; d.pixels = digits; d.palette = digitPal;
  g_fontTex = r.uploadTexture(d);
  if (!g_ballTex || !g_boxTex || !g_fontTex) return false;
  g_font = PackFontRec();
  g_font.glyphW = 4; g_font.glyphH = 6; g_font.firstChar = '0'; g_font.charCount = 10; g_font.columns = 10;
  g_font.lineHeight = 7;
  for (int i = 0; i < 10; ++i) g_font.advance[i] = 4;
  g_spriteCount = count < kMaxStress ? count : kMaxStress;
  for (u32 i = 0; i < g_spriteCount; ++i) {
    Sprite& s = g_sprites[i];
    s.x = rnd() * static_cast<float>(w - 16);
    s.y = rnd() * static_cast<float>(h - 16);
    s.vx = (rnd() - 0.5f) * 240.0f;
    s.vy = (rnd() - 0.5f) * 240.0f;
    s.tex = i & 1;
    s.r = static_cast<u8>(96 + rnd() * 159);
    s.g = static_cast<u8>(96 + rnd() * 159);
    s.b = static_cast<u8>(96 + rnd() * 159);
  }
  return true;
}

// Moves the sprites one fixed step (1/60 s) and bounces them off the virtual screen edges.
void stepStress(u32 w, u32 h) {
  const float dt = 1.0f / 60.0f, maxX = static_cast<float>(w - 16), maxY = static_cast<float>(h - 16);
  for (u32 i = 0; i < g_spriteCount; ++i) {
    Sprite& s = g_sprites[i];
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    if (s.x < 0) { s.x = 0; s.vx = -s.vx; } else if (s.x > maxX) { s.x = maxX; s.vx = -s.vx; }
    if (s.y < 0) { s.y = 0; s.vy = -s.vy; } else if (s.y > maxY) { s.y = maxY; s.vy = -s.vy; }
  }
}

void drawStress(IRenderer& r) {  // OverlayHook: runs inside renderWorld, before endFrame
  r.drawRect(Rect(0, 0, 480, 20), Color32(20, 40, 80, 255), 0);
  for (u32 i = 0; i < g_spriteCount; ++i) {
    const Sprite& s = g_sprites[i];
    r.drawSprite(s.tex ? g_boxTex : g_ballTex, Rect(0, 0, 16, 16), Rect(s.x, s.y, 16, 16), Color32(s.r, s.g, s.b, 255),
                 10 + (i & 3));
  }
  r.drawRect(Rect(4, 4, 36, 12), Color32(0, 0, 0, 200), 100);
  r.drawText(g_fontTex, g_font, 6, 7, g_hud, Color32(255, 220, 80, 255), 101);
}

// ---- helpers -----------------------------------------------------------------------------------------------
double nowMs() {
  return static_cast<double>(SDL_GetPerformanceCounter()) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

// ---- touch overlay -----------------------------------------------------------------------------------------
SdlPlatform* g_platform = nullptr;

void drawTouchOverlay(IRenderer& r) {  // OverlayHook: translucent zones on top of everything
  if (!g_platform || !g_platform->touchOverlayVisible()) return;
  const TouchZones& z = g_platform->touchZones();
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
  bool audioStress = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--pack") && i + 1 < argc) packPath = argv[++i];
    else if (!strcmp(argv[i], "--scene") && i + 1 < argc) startScene = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shotPath = argv[++i];
    else if (!strcmp(argv[i], "--stress-sprites") && i + 1 < argc) stressCount = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--audio-stress")) audioStress = true;
    else if (!strcmp(argv[i], "--check-at") && i + 1 < argc) checkAt = static_cast<u32>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--headless-gl")) {}
    else {
      fprintf(stderr, "usage: rensga_desktop [--pack game.rpak] [--scene name] [--frames N] [--screenshot out.png] [--stress-sprites N] [--audio-stress] [--check-at N] [--headless-gl]\n");
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
  setOverlayHook(drawTouchOverlay);

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
  Save::use(&saves);

  Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes)) {
    RLOG_ERROR("game", "arena allocation failed");
    return 2;
  }
  World world;
  WorldConfig cfg;
  if (!world.init(cfg, arena)) return 2;

  u32 vw = 480, vh = 272;  // PSP profile resolution unless the pack says otherwise
  LoadedPack loaded;
  PackView view;
  const PackConsoleTarget* ct = nullptr;
  u32 packTex[kMaxPackTextures] = {};
  if (stressCount) {
    if (!renderer.init(vw, vh)) return 3;
    platform.setVirtualResolution(vw, vh);
    if (!setupStress(renderer, stressCount, vw, vh)) { RLOG_ERROR("game", "stress setup failed"); return 3; }
    setOverlayHook(drawStress);  // the stress test has no touch controls
    RLOG_INFO("game", "rensga: stress sprites=%u", g_spriteCount);
  } else {
    Span<const u8> bytes = fs.readFile(packFile, arena);
    if (!bytes || !view.open(bytes.data, bytes.size, packFile)) {
      RLOG_ERROR("game", "rensga: cannot read pack %s", packFile);
      return 3;
    }
    ct = view.records<PackConsoleTarget>(pack_section::CTGT);
    if (ct && ct->width && ct->height) { vw = ct->width; vh = ct->height; }
    if (!renderer.init(vw, vh)) return 3;
    platform.setVirtualResolution(vw, vh);
    u32 sceneIndex = 0;
    if (startScene) {
      SceneManager::bind(&view, 0);
      const i32 si = SceneManager::sceneIndexByName(startScene);
      if (si < 0) { RLOG_ERROR("game", "rensga: no scene named %s in the pack", startScene); return 3; }
      sceneIndex = static_cast<u32>(si);
    }
    if (!world.loadPack(view, &loaded, sceneIndex)) {
      RLOG_ERROR("game", "rensga: cannot load pack %s", packFile);
      return 3;
    }
    Input::loadActions(view, loaded.inputs, loaded.inputCount);
    renderer.setMeshes(loaded.meshes);
    const PackAssets& as = loaded.assets;
    for (u32 i = 0; i < as.textureCount && i < kMaxPackTextures; ++i) {
      const PackTexRec& t = as.textures[i];
      TextureDesc d;
      d.width = t.width; d.height = t.height; d.format = static_cast<u8>(t.format);
      d.pixels = as.texturePixels(t); d.palette = as.texturePalette(t);
      packTex[i] = renderer.uploadTexture(d);
    }
    if (as.textureCount) RLOG_INFO("game", "rensga: uploaded %u textures", as.textureCount);
    GfxAssets::bind(&view, &loaded.assets, packTex, as.textureCount);  // 2D components resolve textures here
    SceneManager::bind(&view, sceneIndex);
    u32 pickups = 0;
    world.forEach<MeshRenderer>([&](MeshRenderer& mr) {
      GameObject* go = mr.gameObject();
      if (go && go->name[0] == 'P' && go->name[1] == 'i' && go->name[2] == 'c') ++pickups;
    });
    RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", world.objectCount(), pickups);
  }

  if (audioStress && maxFrames == 0) maxFrames = 600;
  audio.init(&fs, ct, audioClockUs);
  if (!stressCount) audio.registerPackClips(loaded.assets);
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
    const PackAssets& as = loaded.assets;
    for (u32 i = 0; i < as.textureCount && i < kMaxPackTextures; ++i) {
      const PackTexRec& t = as.textures[i];
      TextureDesc d;
      d.width = t.width; d.height = t.height; d.format = static_cast<u8>(t.format);
      d.pixels = as.texturePixels(t); d.palette = as.texturePalette(t);
      packTex[i] = renderer.uploadTexture(d);
    }
    GfxAssets::bind(&view, &loaded.assets, packTex, as.textureCount);
    SceneManager::bind(&view, static_cast<u32>(si));
    audio.registerPackClips(loaded.assets);
    heap::checkReset();  // the reload may allocate; the check window restarts
    RLOG_INFO("game", "rensga: hot-reload %s (scene %s, objects=%u)", watchPath, SceneManager::activeSceneName(), world.objectCount());
    fflush(stdout);
  };
#endif

  GameLoop loop;
  loop.init(world);
  loop.setFixedStepHook(physics::fixedStep, nullptr);

  double last = platform.timeSeconds();
  VirtualGamepad pad;
  u32 frame = 0;
  double sumMs = 0, maxMs = 0;
  while (!platform.shouldQuit() && !Application::quitRequested() && (maxFrames == 0 || frame < maxFrames)) {
    const double t0 = nowMs();
    const double now = platform.timeSeconds();
#if defined(RENSGA_HAS_PACK_WATCHER) && !defined(__ANDROID__)
    if (watcher.watching() && t0 >= nextWatchMs) {  // ~2 polls a second, between frames
      nextWatchMs = t0 + 500.0;
      if (watcher.check()) hotReload();
    }
#endif
    platform.pollInput(pad);
    loop.frame(static_cast<Scalar>(now - last), pad);
    last = now;
    audio.update();
    if (stressCount) {
      stepStress(vw, vh);
      snprintf(g_hud, sizeof(g_hud), "%d", static_cast<int>(g_hudMs * 10.0f));
    }
    renderWorld(world, renderer, stressCount ? Color32(28, 26, 22, 255) : Color32(40, 60, 90, 255));
    ++frame;
    // The first frame includes lazy driver work (shader JIT, buffer setup): the alloc check starts after it.
    if (frame == 1) heap::checkReset();
    if (checkAt && frame == checkAt) heap::logAllocCheck();
    const double ms = nowMs() - t0;
    sumMs += ms;
    if (ms > maxMs) maxMs = ms;
    g_hudMs = static_cast<float>(ms);
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
  platform.shutdown();
  if (allocs != 0 && rc == 0) rc = 4;
  return rc;
}
