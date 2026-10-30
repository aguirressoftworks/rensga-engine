// Debug for game scripts: log lines on the "game" channel with a severity prefix, Assert, and DrawLine.
#pragma once
#include "rensga/core/log.h"
#include "rensga/core/math.h"

namespace rensga {

// One debug line. Stored in a fixed ring buffer that a renderer or the editor reads each frame.
struct DebugLine {
  Vector3 a, b;
  Color32 color = Color32(255, 255, 255, 255);
  Scalar remaining = 0;  // seconds left; ignored for one-frame lines
  bool oneFrame = false; // duration 0: shown for the frame it was drawn in
  bool active = false;
};

constexpr u32 kMaxDebugLines = 256;

struct Debug {
  // Log prints "<text>", LogWarning "Warning: <text>", LogError "Error: <text>" (plus the sink's own level).
  static void Log(const char* fmt, ...) RENSGA_PRINTF_LIKE(1, 2);
  static void LogWarning(const char* fmt, ...) RENSGA_PRINTF_LIKE(1, 2);
  static void LogError(const char* fmt, ...) RENSGA_PRINTF_LIKE(1, 2);
  // Logs "Assertion failed: <msg>" as an error when `condition` is false. Never aborts. Returns `condition`.
  static bool Assert(bool condition, const char* message = "");

  // Draws a line for `duration` seconds (0 = this frame only). The oldest line is overwritten when the ring is full.
  static void DrawLine(const Vector3& a, const Vector3& b, Color32 color = Color32(255, 255, 255, 255), Scalar duration = 0);

  // ---- for renderers / the editor (reading is optional; renderers may ignore lines)
  static constexpr u32 lineCapacity() { return kMaxDebugLines; }
  // Slot i in [0, lineCapacity()); check `.active`.
  static const DebugLine& lineAt(u32 slot);
  static u32 activeLineCount();
  static void clearLines();
  // Ages the lines. World::startPending calls it once per frame with the frame's delta time.
  static void tickLines(Scalar dt);
};

}  // namespace rensga
