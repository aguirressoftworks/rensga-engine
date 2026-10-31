// PSP platform backend (PSP Engine 1, F4.03): SDL2 for input (game controller) and audio (SdlAudio), never SDL video.
// The game owns the module info, the exit/power callback thread and the clock (333 MHz, or RENSGA_PSP_CPU_MHZ); the
// callbacks only set flags. main() stays ours: SDL_MAIN_HANDLED, no SDL2main, SDL_SetMainReady() in init().
// Fallbacks, each with the reason logged ("rensga: input ..."): native sceCtrl through psp_pad::map when SDL cannot
// start or opens no controller (or when built with RENSGA_PSP_NATIVE_PAD).
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/log.h"

namespace rensga {

class PspPlatform : public IPlatform {
 public:
  bool init() override;  // exit callback thread, 333 MHz, analog sampling
  void shutdown() override;
  void pollInput(VirtualGamepad& pad) override;
  double timeSeconds() override;
  bool shouldQuit() override;

  // "sdl" (SDL game controller) or "native" (sceCtrl); valid after init().
  const char* inputBackend() const { return sdlPad_ ? "sdl" : "native"; }
  bool sdlReady() const { return sdlReady_; }  // SDL_Init succeeded (timer + controller); audio may use SdlAudio
  // Power callback counters (set by the callback thread, read by the game): suspends and resumes seen so far.
  static unsigned suspendCount();
  static unsigned resumeCount();

 private:
  void* sdlPad_ = nullptr;  // SDL_GameController*
  bool sdlReady_ = false;
};

// Game folder / device root. Dev builds (Play) read from the PC through "host0:/"; release builds
// (RENSGA_PSP_PRX=ON, which defines RENSGA_RELEASE) read from ms0:/PSP/GAME/<id>/ next to EBOOT.PBP.
// RENSGA_PSP_GAME_ID (compile definition, default "RENSGA") is that folder name.
#ifndef RENSGA_PSP_GAME_ID
#define RENSGA_PSP_GAME_ID "RENSGA"
#endif
#if defined(RENSGA_RELEASE)
#define RENSGA_PSP_DEFAULT_ROOT "ms0:/PSP/GAME/" RENSGA_PSP_GAME_ID "/"
#else
#define RENSGA_PSP_DEFAULT_ROOT "host0:/"
#endif

// IFileSystem contract (Engine 1): "data:/<rel>" and "save:/<rel>" both live under the root above
// (a Memory Stick folder is writable; so is host0:). A path with another device prefix ("host0:", "ms0:",
// "umd0:") is used as is; an unprefixed path is relative to the root. Streams: fixed table of 8 slots. The first
// kAheadStreams open streams (the music) are read ahead by a worker thread into static ring buffers, so the main thread's
// readStream/seekStream copy memory instead of waiting on the Memory Stick; the others read synchronously.
// Picks the data root (the folder that holds game.rpak) and logs the probe: argv[0]'s folder if it has a device prefix,
// then ms0:/PSP/GAME/<id>/, umd0:/PSP_GAME/USRDIR/, host0:/. The first one that holds `probeFile` wins; if none does
// the build's default root (RENSGA_PSP_DEFAULT_ROOT) is returned. `out` receives a path ending in '/'.
// Logs "rensga: data root=<root> (<reason>)" and one "rensga: data probe <root> <found|missing>" line per candidate.
void pspProbeDataRoot(const char* argv0, const char* probeFile, char* out, unsigned outSize);

class PspFileSystem : public IFileSystem {
 public:
  static constexpr int kStreamSlots = 8;
  static constexpr int kAheadStreams = 2;
  static constexpr unsigned kAheadBytes = 32 * 1024;  // ring per read-ahead stream
  explicit PspFileSystem(const char* root = RENSGA_PSP_DEFAULT_ROOT);
  const char* root() const { return root_; }
  Span<const u8> readFile(const char* path, Arena& arena) override;
  void writeLog(const char* text) override;
  // Contract 3 methods, declared without `override` so this header compiles against an older interface.
  i32 openStream(const char* path) override;
  u32 readStream(i32 h, void* dst, u32 bytes) override;
  bool seekStream(i32 h, u32 offset) override;
  void closeStream(i32 h) override;
  bool writeFile(const char* path, const void* data, u32 bytes) override;

 private:
  bool resolve(const char* path, char* out, unsigned outSize) const;
  char root_[128];
  int fds_[kStreamSlots];
  signed char ahead_[kStreamSlots];  // read-ahead ring index of the slot, -1 = synchronous
};

// Installs stdout + host0:/rensga.log sinks (unbuffered, no heap per line).
void pspInstallLogSinks(const char* logPath = "host0:/rensga.log");

}  // namespace rensga
