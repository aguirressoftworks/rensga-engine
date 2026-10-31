// F5: system services scripts can call, each backed by an IPlatform virtual with a default ("not supported"):
// TouchScreenKeyboard (on-screen keyboard), SystemInfo (battery, memory), plus the binding of the platform.
// Names follow Unity; sizes are in Unity's units (MB for the *Size getters). Implemented in engine/core/src/system_services.cpp.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/feature.h"
#include "rensga/core/types.h"

namespace rensga {

namespace system_services {
// The platform the services talk to. GameLoop::run binds its platform; runtime mains and tests may bind explicitly.
void bind(IPlatform* platform);
IPlatform* platform();
}  // namespace system_services

// Unity: TouchScreenKeyboard. One keyboard at a time; there is no heap, the text lives in a fixed buffer.
struct TouchScreenKeyboard {
  enum class Status : u8 { None, Visible, Done, Canceled, LostFocus };  // Unity names; LostFocus = the platform failed it
  static constexpr u32 kMaxText = 64;
  // Opens the keyboard with `initial` text and a title; maxLen is clamped to kMaxText - 1. False when unsupported or busy.
  RENSGA_REQUIRES_FEATURE(OSK, "No on-screen keyboard on this profile; read the text from UIText / a UI field instead.")
  static bool Open(const char* initial, const char* title = "", u32 maxLen = kMaxText - 1);
  // Polls the platform; Done / Canceled stay until the next Open.
  static Status status();
  // The text typed so far (final once status() is Done); the initial text before that.
  static const char* text();
};

// Unity: SystemInfo.
struct SystemInfo {
  enum class BatteryStatus : u8 { Unknown, Charging, Discharging, NotCharging, Full };  // Unity names
  // 0..1, or -1 when unknown (Unity convention).
  RENSGA_REQUIRES_FEATURE(BATTERY, "This profile has no battery information; do not branch on it.")
  static Scalar batteryLevel();
  RENSGA_REQUIRES_FEATURE(BATTERY, "This profile has no battery information; do not branch on it.")
  static BatteryStatus batteryStatus();
  // Total memory in megabytes (0 = unknown).
  static i32 systemMemorySize();
  static i32 graphicsMemorySize();
  // Bytes in use right now (Unity: Profiler.GetTotalAllocatedMemoryLong / GetAllocatedMemoryForGraphicsDriver); 0 = unknown.
  static u32 usedMemory();
  static u32 usedGraphicsMemory();
};

}  // namespace rensga
