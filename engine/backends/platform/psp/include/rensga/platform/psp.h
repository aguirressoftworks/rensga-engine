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
  // ---- F5 system services (psp_system.cpp). The CPU clock, language, confirm button, battery, memory, suspend / Home, the OSK and the
  // savedata dialog sit behind the IPlatform seams (backends.h); scripts reach them through Application, SystemInfo,
  // TouchScreenKeyboard and Save::setMode (core system.h / save.h).
  bool setCpuClock(i32 mhz) override;  // 222 or 333 (bus = half); true when the value reads back
  u32 language() override;             // SystemLanguage value from sceUtilityGetSystemParamInt(LANGUAGE)
  bool confirmButtonEast() override;   // BUTTON_SWAP == circle accepts
  i32 batteryPercent() override;
  i32 batteryStatus() override;
  i32 suspendState() override;  // +1 suspend / Home popup open, -1 resume / popup closed (one transition per call)
  bool beginTextInput(const char* initial, const char* title, u32 maxLen) override;
  i32 pollTextInput(char* out, u32 outSize) override;
  u64 systemMemoryBytes() override;
  u64 graphicsMemoryBytes() override;
  u64 usedSystemMemoryBytes() override;
  u64 usedGraphicsMemoryBytes() override;
  i32 saveDialog(const char* slot, void* data, u32 bytes, bool load) override;
  void setSaveMetadata(const char* title, const char* detail) override;  // F6 (A3): PARAM.SFO title / detail of the next SAVEDATA save

  // Utility dialogs (OSK, savedata) draw through the GE, so they are driven by the runtime main:
  //  - updateDialogs() runs once per frame AFTER the frame's display list is finished and the GE is idle (GuRenderer::flush())
  //    and BEFORE the next endFrame swaps buffers. It is a no-op without an open dialog.
  //  - setFramePump(): saveDialog() blocks (Save::SaveFile is synchronous), so it needs to produce frames itself. The pump must
  //    render one full frame (renderWorld, ending in the swap) and leave the GE idle.
  //  - setGraphicsProbe(): bytes of VRAM in use (the GU texture allocator); the platform cannot see the renderer.
  using FramePump = void (*)(void* user);
  using BytesProbe = u32 (*)(void* user);
  void setFramePump(FramePump pump, void* user) { pump_ = pump; pumpUser_ = user; }
  void setGraphicsProbe(BytesProbe probe, void* user) { vramProbe_ = probe; vramUser_ = user; }
  // Memory the game owns: `total` bytes (the arena the main created from the CTGT budget; the kernel's own free-memory numbers say
  // nothing here because PSP_HEAP_SIZE_KB hands almost all RAM to the C heap) and a probe for the bytes used (arena.used()).
  void setMemoryProbe(u64 total, BytesProbe used, void* user) { memTotal_ = total; memUsed_ = used; memUser_ = user; }
  void updateDialogs();
  bool dialogActive() const { return osk_ != 0; }
  // Dev / test: savedata AUTOSAVE / AUTOLOAD (no UI) instead of the LISTSAVE / LISTLOAD dialog, for headless runs.
  void setSaveDialogAuto(bool on) { saveAuto_ = on; }
  // Power callback counters (set by the callback thread, read by the game): suspends and resumes seen so far.
  static unsigned suspendCount();
  static unsigned resumeCount();

 private:
  void* sdlPad_ = nullptr;  // SDL_GameController*
  bool sdlReady_ = false;
  FramePump pump_ = nullptr;
  void* pumpUser_ = nullptr;
  BytesProbe vramProbe_ = nullptr;
  BytesProbe memUsed_ = nullptr;
  void* memUser_ = nullptr;
  u64 memTotal_ = 0;
  void* vramUser_ = nullptr;
  bool saveAuto_ = false;
  char saveTitle_[32] = {};   // F6 (A3): from SaveSystem::Write, empty = the defaults
  char saveDetail_[32] = {};
  int osk_ = 0;  // 0 closed, 1 open
  i32 oskResult_ = 3;
  u32 delivSuspends_ = 0, delivResumes_ = 0;
  int homeOpen_ = 0;
  unsigned homePoll_ = 0;
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
