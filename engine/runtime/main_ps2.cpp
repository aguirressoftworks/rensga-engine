// PS2 entry point (Engine 2, gsKit): boot -> data:/game.rpak (cdrom0:\GAME.RPK on disc) -> fixed-timestep loop ->
// 2D render through GsRenderer. Without a pack, or when the pack has no textures, built-in CLUT4 art is used.
//
// Stress test (the F2.02 PS2 exercise): RENSGA_PS2_STRESS_SPRITES sprites (default 1000) bounce around the screen with
// per-sprite tint and sort key; a CLUT4 digit font shows the frame counter and the sprite count. Build-time defines:
//   RENSGA_PS2_STRESS_SPRITES=N       sprites per frame (0 = none)
//   RENSGA_PS2_VRAM_STRESS=1          24 textures (3.8 MB) drawn every frame against the ~1.8 MB texture area (VRAM manager test)
//   RENSGA_PS2_CHECK_FRAMES=N         log "rensga: alloc-check frames=N allocs=K" once at frame N (contract 7), default 300
//   RENSGA_PS2_SHOT_FRAME=N           at frame N dump both frame buffers to host:rensga_fb0.raw / fb1.raw (PCSX2 capture)
//   RENSGA_EXIT_AFTER_FRAMES=N        leave the main loop after N frames (CI)
//   RENSGA_PS2_AUDIO_STRESS=N         audio stress: streamed music + 16 looping effects for N frames, logs "rensga: audio-stress ..." (Engine 4)
//   RENSGA_PS2_SAVE_SELFTEST=1        game mode: at frame 20 write save:/selftest, read it back and log "rensga: save selftest"
//   RENSGA_PS2_AUTOSTART=<scene>      game mode: at frame 30 load this scene (the PCSX2 sandbox has no controller input)
// Game mode (F2.05): a pack with tilemaps or sprite clips is played as a 2D game: every TEXR is uploaded, the GameLoop
// runs the scene scripts and renderWorld draws tilemaps, sprites and UI. Saves go to save:/ (memory card, see Ps2FileSystem).
// Heartbeats ("rensga: frame <n> ...") go to the log every 60 frames (contract 8).
#include <stdio.h>
#include <string.h>

#include "rensga/Physics.h"
#include "rensga/core/application.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/input.h"
#include "rensga/core/save.h"
#include "rensga/core/scene_manager.h"
#include "rensga/core/backends.h"
#include "rensga/core/log.h"
#include "rensga/core/loop.h"
#include "rensga/core/memory.h"
#include "rensga/core/world.h"
#include "rensga/platform/ps2.h"
#include "rensga/render/gs.h"
#include "profiler.h"
#include "runtime_audio.h"
#include "runtime_boot.h"
#include "runtime_prof_renderer.h"
#ifdef RENSGA_PS2_VRAM_STRESS
#include "ps2_vram_stress.h"
#endif

#ifndef RENSGA_PS2_STRESS_SPRITES
#define RENSGA_PS2_STRESS_SPRITES 1000
#endif
#ifndef RENSGA_PS2_CHECK_FRAMES
#define RENSGA_PS2_CHECK_FRAMES 300
#endif

using namespace rensga;

namespace {

constexpr u32 kMaxStress = 4096;

// Built-in sprite: 32x32 CLUT4 soft ring. Palette 0 is transparent, 1..15 run from deep orange to pale yellow.
alignas(16) u8 g_spritePixels[32 * 32 / 2];
alignas(16) u32 g_spritePal[16];
// Built-in font: '0'..'9' and ':' as 8x8 cells in an 88x8 CLUT4 strip.
alignas(16) u8 g_fontPixels[88 * 8 / 2];
alignas(16) u32 g_fontPal[16];
PackFontRec g_font;

void buildSprite() {
  g_spritePal[0] = 0;
  for (int i = 1; i < 16; ++i) {
    const u32 r = 255, g = static_cast<u32>(90 + i * 10), b = static_cast<u32>(i * 6);
    g_spritePal[i] = r | (g << 8) | (b << 16) | (255u << 24);
  }
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; x += 2) {
      u8 pair = 0;
      for (int k = 0; k < 2; ++k) {
        const int dx = (x + k) * 2 - 31, dy = y * 2 - 31;  // 0..62 -> -31..31
        const int d2 = dx * dx + dy * dy;                   // radius 31 -> 961
        int idx = 0;
        if (d2 <= 961) idx = d2 < 300 ? 15 : (d2 < 600 ? 9 : 4);  // 3 rings
        pair = static_cast<u8>(pair | (idx << (4 * k)));
      }
      g_spritePixels[(y * 32 + x) / 2] = pair;
    }
}

void buildFont() {
  static const char* glyph[11][7] = {
      {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}, {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
      {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}, {"11110", "00001", "00001", "01110", "00001", "00001", "11110"},
      {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}, {"11111", "10000", "11110", "00001", "00001", "10001", "01110"},
      {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}, {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
      {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}, {"01110", "10001", "10001", "01111", "00001", "00010", "01100"},
      {"00000", "00100", "00100", "00000", "00100", "00100", "00000"}};
  g_fontPal[0] = 0;
  for (int i = 1; i < 16; ++i) g_fontPal[i] = 0xFFFFFFFFu;
  memset(g_fontPixels, 0, sizeof(g_fontPixels));
  for (int c = 0; c < 11; ++c)
    for (int y = 0; y < 7; ++y)
      for (int x = 0; x < 5; ++x)
        if (glyph[c][y][x] == '1') {
          const int px = c * 8 + x + 1, py = y;
          g_fontPixels[(py * 88 + px) / 2] = static_cast<u8>(g_fontPixels[(py * 88 + px) / 2] | (1 << (4 * (px & 1))));
        }
  g_font = PackFontRec();
  g_font.glyphW = g_font.glyphH = 8;
  g_font.firstChar = '0';
  g_font.charCount = 11;
  g_font.columns = 11;
  g_font.lineHeight = 9;
  for (int i = 0; i < 11; ++i) g_font.advance[i] = 7;
}

struct Sprite {
  Scalar x, y, vx, vy, size;
  u32 tint, key;
};
Sprite g_sprites[kMaxStress];

Ps2Platform* g_platformForClock = nullptr;
u64 audioClockUs() { return g_platformForClock ? static_cast<u64>(g_platformForClock->timeSeconds() * 1000000.0) : 0; }

FrameProfiler g_prof;

}  // namespace

int main() {
  RLOG_INFO("game", "rensga: boot (ps2)");
  Application::setPlatform(RuntimePlatform::Ps2);

  static Ps2Platform platform;
  if (!platform.init()) return 2;
  g_platformForClock = &platform;
  static GsRenderer renderer;
  GsVideo video;
  video.pal = platform.info().pal;
  renderer.setVideo(video);
  if (!renderer.init(640, video.pal ? 512 : 448)) {
    RLOG_ERROR("game", "gs renderer init failed");
    return 3;
  }
  static Ps2FileSystem fs;
  RLOG_INFO("game", "rensga: ps2 video=%s region=%c fs=%s", video.pal ? "PAL" : "NTSC", platform.info().region,
            fs.device() == Ps2Device::Disc ? "disc" : (fs.device() == Ps2Device::Host ? "host" : "usb"));

  static Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(20u * 1024 * 1024).arenaBytes)) {
    RLOG_ERROR("game", "arena allocation failed");
    return 1;
  }
  WorldConfig cfg;
  static World world;
  if (!world.init(cfg, arena)) {
    RLOG_ERROR("game", "world init failed");
    return 1;
  }

  // Pack: data:/game.rpak on the host, GAME.RPK on a disc (ISO 9660 level 1 allows only 3 extension characters).
  // bootPack (runtime_boot.h) reads it, uploads every TEXR (stored size and flags included), binds GfxAssets / SceneManager /
  // input actions / saves / audio. The renderer is already initialised (it needs the PAL/NTSC video first); the CTGT view
  // size reaches it through setVirtualResolution so a PAL console keeps the target's 640x448 layout.
  static BootState boot;
  static FileSystemSaveStore saves(fs);
  static RuntimeAudio audio;
  const char* packPath = fs.device() == Ps2Device::Disc ? "data:/game.rpk" : "data:/game.rpak";
  u32 sprite = 0, fontTex = 0;
  static u32 packTex[GfxAssets::kMaxTextures] = {};
  bool gameMode = false;
  Rect spriteSrc(0, 0, 32, 32);
  BootDeps deps;
  deps.fs = &fs; deps.arena = &arena; deps.world = &world;
  deps.packPath = packPath;
  deps.renderer = &renderer; deps.initRenderer = false;
  deps.textureHandles = packTex; deps.textureHandleCapacity = GfxAssets::kMaxTextures;
  deps.defaultWidth = 640; deps.defaultHeight = 448;
  deps.onResolution = [](void* user, u32 w, u32 h) { static_cast<GsRenderer*>(user)->setVirtualResolution(w, h); };
  deps.onResolutionUser = &renderer;
  deps.saves = &saves;
  deps.audio = &audio; deps.audioClock = audioClockUs;
  const bool havePack = bootPack(deps, boot);
  u32 vw = renderer.width(), vh = renderer.height();
  if (havePack) {
    const LoadedPack& loaded = boot.loaded;
    vw = boot.width; vh = boot.height;
    RLOG_INFO("game", "rensga: pack %s objects=%u textures=%u fonts=%u", packPath, world.objectCount(), loaded.assets.textureCount,
              loaded.assets.fontCount);
    gameMode = loaded.assets.tilemapCount > 0 || loaded.assets.animCount > 0 || SceneManager::sceneCount() > 1;  // F6 a3: a multi-scene pack (UI-only levels_save) is a game, not the sprite stress demo
    if (gameMode) {
      RLOG_INFO("game", "rensga: game mode scene=%s scenes=%u", SceneManager::activeSceneName(), SceneManager::sceneCount());
    } else if (loaded.assets.textureCount > 0 && packTex[0]) {
      const PackTexRec& t = loaded.assets.textures[0];
      sprite = packTex[0];
      spriteSrc = Rect(0, 0, static_cast<Scalar>(t.width), static_cast<Scalar>(t.height));
    }
  } else {
    RLOG_WARN("game", "no usable %s, using the built-in sprite art", packPath);
    Save::use(&saves);
    audio.init(&fs, nullptr, audioClockUs);
  }
  buildSprite();
  buildFont();
  if (!sprite) {
    TextureDesc d;
    d.width = d.height = 32; d.format = tex_format::CLUT4; d.pixels = g_spritePixels; d.palette = g_spritePal;
    sprite = renderer.uploadTexture(d);
  }
  {
    TextureDesc d;
    d.width = 88; d.height = 8; d.format = tex_format::CLUT4; d.pixels = g_fontPixels; d.palette = g_fontPal;
    fontTex = renderer.uploadTexture(d);
  }
#ifdef RENSGA_PS2_VRAM_STRESS
  if (!vramstress::init(renderer)) RLOG_ERROR("game", "vram stress texture upload failed");
#endif
  if (!sprite || !fontTex) RLOG_ERROR("game", "texture upload failed sprite=%u font=%u", sprite, fontTex);

  // Audio (Engine 4): bootPack set up the mixer + audsrv and registered the pack clips. Music streams from the disc root (MUSIC.WAV).
#ifdef RENSGA_PS2_AUDIO_STRESS
  audio.beginStress(havePack ? &boot.loaded.assets : nullptr);
#else
  audio.startMusic();
#endif

  const u32 stress = gameMode ? 0 : RENSGA_PS2_STRESS_SPRITES < kMaxStress ? RENSGA_PS2_STRESS_SPRITES : kMaxStress;
  Random::InitState(12345);
  const Scalar W = static_cast<Scalar>(vw), H = static_cast<Scalar>(vh);  // the target's view (virtual) size
  for (u32 i = 0; i < stress; ++i) {
    Sprite& s = g_sprites[i];
    s.size = Random::Range(16.0f, 48.0f);
    s.x = Random::Range(0.0f, W - s.size);
    s.y = Random::Range(0.0f, H - s.size);
    s.vx = Random::Range(-3.0f, 3.0f);
    s.vy = Random::Range(-3.0f, 3.0f);
    const i32 lo = 128, hi = 256;
    const u32 r = static_cast<u32>(Random::Range(lo, hi)), g = static_cast<u32>(Random::Range(lo, hi)), b = static_cast<u32>(Random::Range(lo, hi));
    s.tint = r | (g << 8) | (b << 16) | (255u << 24);
    s.key = static_cast<u32>(Random::Range(static_cast<i32>(0), static_cast<i32>(16)));
  }

  static GameLoop loop;
  loop.init(world, LoopConfig::forRefreshHz(video.pal ? 50 : 60));
  Input::bindPlatform(&platform);
  loop.setFixedStepHook(physics::fixedStep, nullptr);
  g_prof.init(audioClockUs);
  static ProfilingRenderer profRenderer(renderer, g_prof);  // marks Render just before endFrame (which waits for vsync)

  RLOG_INFO("game", "rensga: stress sprites=%u virtual=%ux%u", stress, vw, vh);
  double last = platform.timeSeconds(), windowStart = last;
  VirtualGamepad pad;
  u32 frame = 0;
  while (!platform.shouldQuit() && !Application::quitRequested()) {
    g_prof.frameBegin();
    const double now = platform.timeSeconds();
    platform.pollInput(pad);
    loop.frame(static_cast<Scalar>(now - last), pad);
    last = now;
    g_prof.mark(ProfSection::Update);

    if (gameMode) {
      renderWorld(world, profRenderer, Color32(28, 26, 22, 255));
    } else {
    renderer.beginFrame(Color32(28, 26, 22, 255));
    for (u32 i = 0; i < stress; ++i) {
      Sprite& s = g_sprites[i];
      s.x += s.vx; s.y += s.vy;
      if (s.x < 0 || s.x > W - s.size) { s.vx = -s.vx; s.x += s.vx; }
      if (s.y < 0 || s.y > H - s.size) { s.vy = -s.vy; s.y += s.vy; }
      Color32 tint(static_cast<u8>(s.tint), static_cast<u8>(s.tint >> 8), static_cast<u8>(s.tint >> 16), 255);
      renderer.drawSprite(sprite, spriteSrc, Rect(s.x, s.y, s.size, s.size), tint, s.key);
    }
#ifdef RENSGA_PS2_VRAM_STRESS
    vramstress::draw(renderer, frame, fontTex, g_font);
#endif
    renderer.drawRect(Rect(0, 0, 220, 12), Color32(0, 0, 0, 160), 100);
    char hud[48];
    snprintf(hud, sizeof(hud), "%u:%u", frame, stress);
    renderer.drawText(fontTex, g_font, 4, 2, hud, Color32(255, 255, 255, 255), 101);
    g_prof.mark(ProfSection::Render);
    renderer.endFrame();
    }
    g_prof.mark(ProfSection::Present);
    audio.update();
    g_prof.mark(ProfSection::Audio);
    g_prof.frameEnd();
    ++frame;

    if (frame % 60 == 0) {
      const GsStats& st = renderer.stats();
      const double dt = platform.timeSeconds() - windowStart;
      windowStart += dt;
      RLOG_INFO("game", "rensga: frame %u ms=%.2f cpu_ms=%.2f sprites=%u glyphs=%u dropped=%u uploads=%u kicks=%u evictions=%u vram=%uKB", frame,
                dt * 1000.0 / 60.0, st.cpuMicros / 1000.0, st.sprites, st.glyphs, st.dropped, st.uploads, st.kicks, st.evictions, st.vramUsed / 1024);
    }
#ifdef RENSGA_PS2_SAVE_SELFTEST
    if (gameMode && frame == 20) {
      const i32 old = Save::LoadInt("selftest", -1);
      const bool wrote = Save::SaveInt("selftest", static_cast<i32>(frame) + 1000);
      RLOG_INFO("game", "rensga: save selftest previous=%d wrote=%d readback=%d", static_cast<int>(old), wrote ? 1 : 0,
                static_cast<int>(Save::LoadInt("selftest", -1)));
    }
#endif
#ifdef RENSGA_PS2_AUTOSTART
    if (gameMode && frame == 30) SceneManager::LoadScene(RENSGA_PS2_AUTOSTART);
#endif
    if (frame == RENSGA_PS2_CHECK_FRAMES) heap::logAllocCheck();
#ifdef RENSGA_PS2_SHOT_FRAME
    if (frame == RENSGA_PS2_SHOT_FRAME) renderer.dumpFrameBuffers("host:rensga_fb");
#endif
#ifdef RENSGA_PS2_AUDIO_STRESS
    if (frame == RENSGA_PS2_AUDIO_STRESS) {
      audio.logStress(frame);
      heap::logAllocCheck();
      break;
    }
#endif
#ifdef RENSGA_EXIT_AFTER_FRAMES
    if (frame >= RENSGA_EXIT_AFTER_FRAMES) break;
#endif
  }
#ifndef RENSGA_PS2_AUDIO_STRESS
  if (frame < RENSGA_PS2_CHECK_FRAMES) heap::logAllocCheck();
#endif
  RLOG_INFO("game", "rensga: quit after %u frames", frame);
  log::flushAll();
  Application::runQuitHooks();  // F6 a3: PlayerPrefs writes itself on a clean exit
  audio.shutdown();
  world.shutdown();
  platform.shutdown();
  return 0;
}
