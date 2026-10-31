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

// F5.11 per-scene budgets. A limit of 0 means "no limit". Triangles and VRAM come from the ConsoleTarget (CTGT) of the pack; the draw
// and texture limits are development heuristics for the PSP (a draw is one mesh submission, GU cost grows with it), not hardware limits.
constexpr u32 kDefaultDrawBudget = 300;
constexpr u32 kDefaultTextureBudget = 64;
struct ProfBudget {
  u32 draws = 0, triangles = 0, textures = 0, vramBytes = 0;
};
struct ProfCounts {
  u32 draws = 0, triangles = 0, textures = 0, vramBytes = 0;
};

enum class ProfSection : u32 { Update = 0, Render, Present, Audio, Count };

class FrameProfiler {
 public:
  using ClockUsFn = u64 (*)();
  // `clock` returns microseconds; null disables the profiler (every call becomes a no-op).
  void init(ClockUsFn clock, u32 logEvery = 60);
  void frameBegin();
  void mark(ProfSection s);
  void frameEnd();

  // F5.11: scene budgets. setScene names the scene in the log (call again after a scene change: the window restarts).
  // noteDraws is called once per frame by the render wrapper, noteResources by the main; the window keeps the maximum of each.
  // frameEnd then logs, next to the "rensga: prof" line (same cadence):
  //   rensga: budget scene=<name> draws=<max>/<limit> tris=<max>/<limit> textures=<n>/<limit> vram=<bytes>/<limit> status=ok|OVER(<kinds>)
  // and a warning "rensga: budget exceeded ..." the first time a kind is over in a scene. `-` means no limit.
  void setBudget(const ProfBudget& b);
  void setScene(const char* name);
  void noteDraws(u32 draws, u32 triangles);
  void noteResources(u32 textures, u32 vramBytes);
  const ProfCounts& peak() const { return peak_; }       // maxima of the current window
  const ProfBudget& budget() const { return budget_; }
  // Bit 0 draws, 1 triangles, 2 textures, 3 vram: kinds over their limit in the current window.
  u32 overMask() const;

 private:
  static constexpr u32 kN = static_cast<u32>(ProfSection::Count);
  ClockUsFn clock_ = nullptr;
  u32 logEvery_ = 60, frames_ = 0;
  u64 last_ = 0, start_ = 0;
  u64 sum_[kN + 1] = {}, max_[kN + 1] = {};  // index kN = whole frame
  u64 cur_[kN] = {};
  ProfBudget budget_;
  ProfCounts peak_;
  bool haveCounts_ = false;
  char scene_[32] = {};
  u32 warned_ = 0;
};

}  // namespace rensga
