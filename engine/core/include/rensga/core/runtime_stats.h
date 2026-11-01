// F6 (A3, API 12): counters the budget queries (rensga/Budget.h) read. The runtime fills them: FrameProfiler (engine/runtime/profiler.cpp)
// sets triangles and VRAM from the renderer wrapper and the texture table, the main binds the mixer. 0 = unknown.
#pragma once
#include "rensga/core/types.h"

namespace rensga {
class AudioMixer;
namespace runtime_stats {
void setTriangles(u32 trianglesLastFrame);
u32 triangles();
void setVram(u32 usedBytes);
u32 vram();
void bindMixer(const AudioMixer* mixer);  // null unbinds
u32 voicesUsed();
u32 voiceLimit();
void reset();
}  // namespace runtime_stats
}  // namespace rensga
