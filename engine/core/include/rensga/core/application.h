// Application: Unity-style runtime facts. The platform / runtime main sets platform and the frame-rate cap;
// scripts call Quit() and read the rest.
#pragma once
#include "rensga/core/types.h"

namespace rensga {

enum class RuntimePlatform : u8 { LinuxPlayer, WindowsPlayer, Android, Psp, Ps2, Psx, Headless };

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
  // Back to defaults (tests, editor play-mode restarts).
  static void reset();
};

}  // namespace rensga
