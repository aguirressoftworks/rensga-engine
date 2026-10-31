// Application: Unity-style runtime facts. The platform / runtime main sets platform and the frame-rate cap;
// scripts call Quit() and read the rest.
#pragma once
#include "rensga/core/feature.h"
#include "rensga/core/types.h"

namespace rensga {

enum class RuntimePlatform : u8 { LinuxPlayer, WindowsPlayer, Android, Psp, Ps2, Psx, Headless };

// Unity names. The numeric values are the engine's own (stable, used by IPlatform::language); Unknown = the platform did not say.
enum class SystemLanguage : u32 {
  English = 0, Japanese, French, Spanish, German, Italian, Dutch, Portuguese, Russian, Korean, ChineseTraditional, ChineseSimplified,
  Unknown = 0xFFFFFFFFu
};

struct Application {
  // Asks the main loop to finish after the current frame. The loop and platform check quitRequested().
  static void Quit();
  static bool quitRequested();
  // True while the game runs (the editor sets it false in edit mode). Default true.
  static bool isPlaying();
  static void setPlaying(bool playing);
  // Frame-rate cap in frames per second; -1 = platform default, 0 is treated like -1.
  static i32 targetFrameRate();
  static void setTargetFrameRate(i32 fps);
  // Defaults to the platform this binary was compiled for; runtime mains override it (Headless for the sim).
  static RuntimePlatform platform();
  static void setPlatform(RuntimePlatform p);
  static const char* platformName(RuntimePlatform p);
  // F5.16: CPU clock in MHz (PSP: 222 or 333). Returns false when the platform has no such control or refuses the value.
  // The editor and desktop builds return false and change nothing.
  RENSGA_REQUIRES_FEATURE(CPUCLOCK, "This profile has no CPU clock control; remove the call.")
  static bool SetCpuClock(i32 mhz);
  // Unity: Application.systemLanguage. Unknown on platforms that do not report one (scripts treat it as English).
  static SystemLanguage systemLanguage();
  // F5.13: true when the platform's confirm button is the east face button (PSP in Japan: circle confirms). Scripts
  // use it to pick the confirm and cancel buttons of menus.
  static bool confirmButtonEast();
  // Back to defaults (tests, editor play-mode restarts).
  static void reset();
};

}  // namespace rensga
