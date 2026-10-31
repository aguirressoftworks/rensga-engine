#include "profiler.h"

#include "rensga/core/log.h"

namespace rensga {

void FrameProfiler::init(ClockUsFn clock, u32 logEvery) {
  *this = FrameProfiler();
  clock_ = clock;
  logEvery_ = logEvery ? logEvery : 60;
}

void FrameProfiler::frameBegin() {
  if (!clock_) return;
  start_ = last_ = clock_();
  for (u32 i = 0; i < kN; ++i) cur_[i] = 0;
}

void FrameProfiler::mark(ProfSection s) {
  if (!clock_ || s >= ProfSection::Count) return;
  const u64 now = clock_();
  cur_[static_cast<u32>(s)] += now - last_;
  last_ = now;
}

void FrameProfiler::frameEnd() {
  if (!clock_) return;
  const u64 total = clock_() - start_;
  for (u32 i = 0; i < kN; ++i) {
    sum_[i] += cur_[i];
    if (cur_[i] > max_[i]) max_[i] = cur_[i];
  }
  sum_[kN] += total;
  if (total > max_[kN]) max_[kN] = total;
  if (++frames_ < logEvery_) return;
  const u64 n = frames_;
  auto avg = [&](u32 i) { return static_cast<unsigned>(sum_[i] / n); };
  auto mx = [&](u32 i) { return static_cast<unsigned>(max_[i]); };
  RLOG_INFO("game", "rensga: prof frames=%u total_us=%u/%u update_us=%u/%u render_us=%u/%u present_us=%u/%u audio_us=%u/%u",
            static_cast<unsigned>(n), avg(kN), mx(kN), avg(0), mx(0), avg(1), mx(1), avg(2), mx(2), avg(3), mx(3));
  frames_ = 0;
  for (u32 i = 0; i <= kN; ++i) sum_[i] = max_[i] = 0;
}

}  // namespace rensga
