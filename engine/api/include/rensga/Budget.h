// F6 contract (C0): read-only budget queries so a game can adapt (spawn fewer particles near the limit). The limits come from the
// ConsoleTarget (CTGT); 0 means "unknown / no limit" (a desktop build without a console target, or a figure the platform cannot
// report). Stub until A3 (Stage 1, API 12): everything returns 0. Defined in engine/api/src/scene_api.cpp.
#pragma once
#include "rensga/core/types.h"

namespace rensga {
struct Budget {
  static u32 memoryUsed();   // bytes of the runtime arena in use
  static u32 memoryLimit();  // bytes the ConsoleTarget allows
  static u32 vramUsed();
  static u32 vramLimit();
  static u32 trianglesLastFrame();
  static u32 trianglesLimit();
  static u32 voicesUsed();   // active mixer voices
  static u32 voicesLimit();
  // 0..100 of the tighter of memory and VRAM, for quick "am I close" checks; 0 when unknown.
  static u32 percentUsed();
};
}  // namespace rensga
