// F4 contract: per-frame section timings for the "rensga: prof" log line (F4.06). Core types only, no heap.
//
//   FrameProfiler prof;  prof.init(clockUs);
//   each frame:  prof.frameBegin();  ...update...  prof.mark(ProfSection::Update);  ...render...  prof.mark(ProfSection::Render);
//                ...present...  prof.mark(ProfSection::Present);  ...audio...  prof.mark(ProfSection::Audio);  prof.frameEnd();
// mark() charges the time since the previous mark (or frameBegin) to the section. frameEnd() logs one line every
// `logEvery` frames and resets the window:
//   rensga: prof frames=60 total_us=<avg>/<max> update_us=<avg>/<max> render_us=<avg>/<max> present_us=<avg>/<max> audio_us=<avg>/<max>
#pragma once
#include "rensga/core/types.h"

namespace rensga {

enum class ProfSection : u32 { Update = 0, Render, Present, Audio, Count };

class FrameProfiler {
 public:
  using ClockUsFn = u64 (*)();
  // `clock` returns microseconds; null disables the profiler (every call becomes a no-op).
  void init(ClockUsFn clock, u32 logEvery = 60);
  void frameBegin();
  void mark(ProfSection s);
  void frameEnd();

 private:
  static constexpr u32 kN = static_cast<u32>(ProfSection::Count);
  ClockUsFn clock_ = nullptr;
  u32 logEvery_ = 60, frames_ = 0;
  u64 last_ = 0, start_ = 0;
  u64 sum_[kN + 1] = {}, max_[kN + 1] = {};  // index kN = whole frame
  u64 cur_[kN] = {};
};

}  // namespace rensga
