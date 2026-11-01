#include "profiler.h"

#include <cstdio>

#include "rensga/core/log.h"
#include "rensga/core/runtime_stats.h"
#include "rensga/core/script_samples.h"

namespace rensga {

void FrameProfiler::setBudget(const ProfBudget& b) { budget_ = b; }

void FrameProfiler::setScene(const char* name) {
  size_t i = 0;
  for (; name && name[i] && i + 1 < sizeof(scene_); ++i) scene_[i] = name[i];
  scene_[i] = 0;
  peak_ = ProfCounts();
  haveCounts_ = false;
  warned_ = 0;
}

void FrameProfiler::noteDraws(u32 draws, u32 triangles) {
  haveCounts_ = true;
  runtime_stats::setTriangles(triangles);  // F6: Budget::trianglesLastFrame
  if (draws > peak_.draws) peak_.draws = draws;
  if (triangles > peak_.triangles) peak_.triangles = triangles;
}

void FrameProfiler::noteResources(u32 textures, u32 vramBytes) {
  haveCounts_ = true;
  runtime_stats::setVram(vramBytes);  // F6: Budget::vramUsed
  if (textures > peak_.textures) peak_.textures = textures;
  if (vramBytes > peak_.vramBytes) peak_.vramBytes = vramBytes;
}

u32 FrameProfiler::overMask() const {
  u32 m = 0;
  if (budget_.draws && peak_.draws > budget_.draws) m |= 1;
  if (budget_.triangles && peak_.triangles > budget_.triangles) m |= 2;
  if (budget_.textures && peak_.textures > budget_.textures) m |= 4;
  if (budget_.vramBytes && peak_.vramBytes > budget_.vramBytes) m |= 8;
  return m;
}

void FrameProfiler::init(ClockUsFn clock, u32 logEvery) {
  *this = FrameProfiler();
  clock_ = clock;
  logEvery_ = logEvery ? logEvery : 60;
  script_samples::setClock(clock);  // F6: Profiler::BeginSample uses the same microsecond clock
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
  script_samples::endFrame();  // F6: roll the script samples of this frame into the window
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
  // F6: script samples (Profiler::BeginSample): one line per name, in first-use order.
  for (u32 i = 0; i < script_samples::windowCount(); ++i) {
    script_samples::WindowEntry e;
    if (!script_samples::windowEntry(i, &e) || e.calls == 0) continue;
    RLOG_INFO("game", "rensga: prof sample name=%s frames=%u calls=%u avg_us=%u max_us=%u", e.name, static_cast<unsigned>(n), e.calls,
              static_cast<unsigned>(e.totalUs / n), e.maxFrameUs);  // avg = per frame over the whole window
  }
  script_samples::resetWindow();
  if (haveCounts_) {
    char lim[4][16];
    const u32 l[4] = {budget_.draws, budget_.triangles, budget_.textures, budget_.vramBytes};
    for (int i = 0; i < 4; ++i) {
      if (l[i]) snprintf(lim[i], sizeof(lim[i]), "%u", static_cast<unsigned>(l[i]));
      else snprintf(lim[i], sizeof(lim[i]), "-");
    }
    const u32 over = overMask();
    char status[40] = "ok";
    if (over) snprintf(status, sizeof(status), "OVER(%s%s%s%s)", over & 1 ? "draws," : "", over & 2 ? "tris," : "", over & 4 ? "textures," : "",
                       over & 8 ? "vram," : "");
    RLOG_INFO("game", "rensga: budget scene=%s draws=%u/%s tris=%u/%s textures=%u/%s vram=%u/%s status=%s", scene_[0] ? scene_ : "-",
              static_cast<unsigned>(peak_.draws), lim[0], static_cast<unsigned>(peak_.triangles), lim[1], static_cast<unsigned>(peak_.textures),
              lim[2], static_cast<unsigned>(peak_.vramBytes), lim[3], status);
    const u32 fresh = over & ~warned_;
    if (fresh) {
      warned_ |= fresh;
      RLOG_WARN("game", "rensga: budget exceeded scene=%s kinds=0x%x (draws %u/%s, tris %u/%s, textures %u/%s, vram %u/%s)",
                scene_[0] ? scene_ : "-", static_cast<unsigned>(fresh), static_cast<unsigned>(peak_.draws), lim[0],
                static_cast<unsigned>(peak_.triangles), lim[1], static_cast<unsigned>(peak_.textures), lim[2],
                static_cast<unsigned>(peak_.vramBytes), lim[3]);
    }
    const u32 keepTex = peak_.textures, keepVram = peak_.vramBytes;
    peak_ = ProfCounts();
    peak_.textures = keepTex;  // resources are re-noted every frame; keep the last value so a window with no note still prints them
    peak_.vramBytes = keepVram;
  }
  frames_ = 0;
  for (u32 i = 0; i <= kN; ++i) sum_[i] = max_[i] = 0;
}

}  // namespace rensga
