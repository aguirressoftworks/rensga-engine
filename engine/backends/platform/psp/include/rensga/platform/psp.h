// PSP platform backend: native sceCtrl/sceDisplay/exit callback/clock (MVP-23, -24, -28).
// Deviation: the design doc routes PSP through SDL2; native calls are used in the MVP because
// SDL2 + GU coexistence is deferred to PSP Engine 1 (the IPlatform interface stays the same).
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
// "umd0:") is used as is; an unprefixed path is relative to the root. Streams: fixed table of 8 slots.
class PspFileSystem : public IFileSystem {
 public:
  static constexpr int kStreamSlots = 8;
  explicit PspFileSystem(const char* root = RENSGA_PSP_DEFAULT_ROOT);
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
  char root_[64];
  int fds_[kStreamSlots];
};

// Installs stdout + host0:/rensga.log sinks (unbuffered, no heap per line).
void pspInstallLogSinks(const char* logPath = "host0:/rensga.log");

}  // namespace rensga
