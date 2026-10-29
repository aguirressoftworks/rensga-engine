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

// Path slots: "host0:" (dev Play), "ms0:" and "umd0:" reserved. Relative paths use the default root.
class PspFileSystem : public IFileSystem {
 public:
  explicit PspFileSystem(const char* root = "host0:/") ;
  Span<const u8> readFile(const char* path, Arena& arena) override;
  void writeLog(const char* text) override;

 private:
  char root_[64];
};

// Installs stdout + host0:/rensga.log sinks (unbuffered, no heap per line).
void pspInstallLogSinks(const char* logPath = "host0:/rensga.log");

}  // namespace rensga
