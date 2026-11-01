// F6 (A3, API 12): the sample table behind Profiler::BeginSample / EndSample (rensga/Profiler.h). Core only, no heap: 32 names, 8
// nested samples. Names are string literals; they are matched by pointer first, then by text. A frame is rolled when the frame
// counter (Time::frameCount) changes or when the runtime profiler calls endFrame(). The runtime FrameProfiler prints the window as
//   rensga: prof sample name=<n> frames=<F> calls=<C> avg_us=<per frame> max_us=<worst frame>
#pragma once
#include "rensga/core/types.h"

namespace rensga {
namespace script_samples {
constexpr u32 kMaxDepth = 8;
constexpr u32 kMaxNames = 32;
using ClockUsFn = u64 (*)();

void setClock(ClockUsFn clock);  // microseconds; null = the bound platform's timeSeconds(), or 0 without one
void begin(const char* name);
void end();
u32 lastMicros(const char* name);  // total microseconds in `name` during the last finished frame
void endFrame();                   // rolls the running frame into the window (idempotent)

// The window since the last resetWindow(): for entry i < windowCount().
struct WindowEntry {
  const char* name = "";
  u32 frames = 0;   // frames rolled in the window
  u32 calls = 0;    // BeginSample calls
  u64 totalUs = 0;
  u32 maxFrameUs = 0;
};
// F6.06: the samples of the most recent rolled frame only (false for entries not sampled in it). `parent` is the entry index of the
// sample that was open when this one first began (-1 = top level): the hierarchy the Profiler window shows.
struct LastFrameEntry {
  const char* name = "";
  u32 us = 0, calls = 0;
  i32 parent = -1;
};
bool lastFrameEntry(u32 i, LastFrameEntry* out);
u32 windowCount();
bool windowEntry(u32 i, WindowEntry* out);
void resetWindow();
void reset();  // tests: forget everything
}  // namespace script_samples
}  // namespace rensga
