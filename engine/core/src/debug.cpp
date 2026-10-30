#include "rensga/core/debug.h"

#include <stdarg.h>
#include <stdio.h>

namespace rensga {

namespace {
DebugLine g_lines[kMaxDebugLines];
u32 g_next = 0;  // ring write position

void emit(LogLevel level, const char* prefix, const char* fmt, va_list ap) {
  char buf[400];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  log::write(level, "game", "%s%s", prefix, buf);
}
}  // namespace

void Debug::Log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(LogLevel::Info, "", fmt, ap);
  va_end(ap);
}
void Debug::LogWarning(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(LogLevel::Warn, "Warning: ", fmt, ap);
  va_end(ap);
}
void Debug::LogError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(LogLevel::Error, "Error: ", fmt, ap);
  va_end(ap);
}

bool Debug::Assert(bool condition, const char* message) {
  if (!condition) log::write(LogLevel::Error, "game", "Assertion failed: %s", message ? message : "");
  return condition;
}

void Debug::DrawLine(const Vector3& a, const Vector3& b, Color32 color, Scalar duration) {
  DebugLine& l = g_lines[g_next];
  g_next = (g_next + 1) % kMaxDebugLines;
  l.a = a;
  l.b = b;
  l.color = color;
  l.oneFrame = duration <= 0;
  l.remaining = duration;
  l.active = true;
}

const DebugLine& Debug::lineAt(u32 slot) { return g_lines[slot % kMaxDebugLines]; }

u32 Debug::activeLineCount() {
  u32 n = 0;
  for (u32 i = 0; i < kMaxDebugLines; ++i) n += g_lines[i].active ? 1u : 0u;
  return n;
}

void Debug::clearLines() {
  for (u32 i = 0; i < kMaxDebugLines; ++i) g_lines[i] = DebugLine();
  g_next = 0;
}

void Debug::tickLines(Scalar dt) {
  // A line drawn during frame N is rendered after frame N and ages here, at the start of frame N + 1.
  for (u32 i = 0; i < kMaxDebugLines; ++i) {
    DebugLine& l = g_lines[i];
    if (!l.active) continue;
    if (l.oneFrame) {
      l.active = false;
    } else {
      l.remaining -= dt;
      if (l.remaining <= 0) l.active = false;
    }
  }
}

}  // namespace rensga
