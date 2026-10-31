// F5: TouchScreenKeyboard and SystemInfo over the bound IPlatform. The platform side (OSK dialog, battery, memory) is the
// platform backend's job (agent A5); this file only holds the state and the defaults.
#include <string.h>

#include "rensga/core/system.h"

namespace rensga {

namespace {
IPlatform* g_platform = nullptr;
char g_text[TouchScreenKeyboard::kMaxText] = {};
TouchScreenKeyboard::Status g_status = TouchScreenKeyboard::Status::None;
u32 mb(u64 bytes) { return static_cast<u32>(bytes / (1024ull * 1024ull)); }
u32 clampU32(u64 v) { return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<u32>(v); }
}  // namespace

void system_services::bind(IPlatform* platform) { g_platform = platform; }
IPlatform* system_services::platform() { return g_platform; }

bool TouchScreenKeyboard::Open(const char* initial, const char* title, u32 maxLen) {
  if (g_status == Status::Visible) return false;
  if (maxLen == 0 || maxLen > kMaxText - 1) maxLen = kMaxText - 1;
  strncpy(g_text, initial ? initial : "", kMaxText - 1);
  g_text[kMaxText - 1] = 0;
  g_status = Status::None;
  if (!g_platform || !g_platform->beginTextInput(g_text, title ? title : "", maxLen)) return false;
  g_status = Status::Visible;
  return true;
}

TouchScreenKeyboard::Status TouchScreenKeyboard::status() {
  if (g_status == Status::Visible && g_platform) {
    char out[kMaxText] = {};
    switch (g_platform->pollTextInput(out, kMaxText)) {
      case 0: break;
      case 1:
        out[kMaxText - 1] = 0;
        memcpy(g_text, out, kMaxText);
        g_status = Status::Done;
        break;
      case 2: g_status = Status::Canceled; break;
      default: g_status = Status::LostFocus; break;
    }
  }
  return g_status;
}

const char* TouchScreenKeyboard::text() { return g_text; }

Scalar SystemInfo::batteryLevel() {
  const i32 pct = g_platform ? g_platform->batteryPercent() : -1;
  return pct < 0 ? -1.0f : static_cast<Scalar>(pct > 100 ? 100 : pct) / 100.0f;
}
SystemInfo::BatteryStatus SystemInfo::batteryStatus() {
  const i32 s = g_platform ? g_platform->batteryStatus() : 0;
  return s >= 1 && s <= 4 ? static_cast<BatteryStatus>(s) : BatteryStatus::Unknown;
}
i32 SystemInfo::systemMemorySize() { return g_platform ? static_cast<i32>(mb(g_platform->systemMemoryBytes())) : 0; }
i32 SystemInfo::graphicsMemorySize() { return g_platform ? static_cast<i32>(mb(g_platform->graphicsMemoryBytes())) : 0; }
u32 SystemInfo::usedMemory() { return g_platform ? clampU32(g_platform->usedSystemMemoryBytes()) : 0; }
u32 SystemInfo::usedGraphicsMemory() { return g_platform ? clampU32(g_platform->usedGraphicsMemoryBytes()) : 0; }

}  // namespace rensga
