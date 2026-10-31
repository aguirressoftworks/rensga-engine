// F5.11: the per-scene budget lines of FrameProfiler ("rensga: budget ..."), the over-budget warning (once per scene and kind)
// and ProfilingRenderer's draw / triangle counts.
#include <string.h>

#include "../rensga_test.h"
#include "profiler.h"
#include "runtime_prof_renderer.h"
#include "rensga/core/log.h"

using namespace rensga;

namespace {
u64 g_now = 0;
u64 fakeClock() { return g_now += 100; }

bool contains(const RingSink& r, const char* needle, LogLevel level = LogLevel::Info) {
  for (u32 i = 0; i < r.count(); ++i) {
    const RingSink::Entry* e = r.entry(i);
    if (e && e->level == level && strstr(e->message, needle)) return true;
  }
  return false;
}
}  // namespace

TEST(profiler_budget_ok_line) {
  RingSink ring;
  log::clearSinks();
  log::addSink(&ring);
  FrameProfiler p;
  p.init(fakeClock, 3);
  ProfBudget b;
  b.draws = 10; b.triangles = 1000; b.textures = 4; b.vramBytes = 4096;
  p.setBudget(b);
  p.setScene("Room");
  for (int i = 0; i < 3; ++i) {
    p.frameBegin();
    p.noteDraws(5 + i, 100 * (i + 1));
    p.noteResources(2, 2048);
    p.frameEnd();
  }
  CHECK(contains(ring, "rensga: budget scene=Room draws=7/10 tris=300/1000 textures=2/4 vram=2048/4096 status=ok"));
  CHECK_EQ(p.overMask(), 0u);
  log::clearSinks();
}

TEST(profiler_budget_over_warns_once) {
  RingSink ring;
  log::clearSinks();
  log::addSink(&ring);
  FrameProfiler p;
  p.init(fakeClock, 2);
  ProfBudget b;
  b.draws = 3; b.triangles = 0;  // 0 = no limit
  p.setBudget(b);
  p.setScene("Big");
  for (int w = 0; w < 2; ++w) {
    for (int i = 0; i < 2; ++i) {
      p.frameBegin();
      p.noteDraws(9, 99999);
      p.frameEnd();
    }
  }
  CHECK(contains(ring, "status=OVER(draws,)"));
  CHECK(contains(ring, "tris=99999/-"));
  u32 warns = 0;
  for (u32 i = 0; i < ring.count(); ++i)
    if (ring.entry(i)->level == LogLevel::Warn && strstr(ring.entry(i)->message, "budget exceeded")) ++warns;
  CHECK_EQ(warns, 1u);  // the second window is over again but already reported for this scene
  p.setScene("Other");
  p.frameBegin(); p.noteDraws(9, 1); p.frameEnd(); p.frameBegin(); p.noteDraws(9, 1); p.frameEnd();
  warns = 0;
  for (u32 i = 0; i < ring.count(); ++i)
    if (ring.entry(i)->level == LogLevel::Warn && strstr(ring.entry(i)->message, "budget exceeded")) ++warns;
  CHECK_EQ(warns, 2u);  // a new scene reports again
  log::clearSinks();
}

TEST(profiler_renderer_counts_draws_and_triangles) {
  NullRenderer inner;
  FrameProfiler p;
  p.init(fakeClock, 1000);
  ProfilingRenderer pr(inner, p);
  PackMeshRec recs[2] = {{0, 30, 0, 0}, {0, 9, 0, 0}};
  MeshTable t;
  t.meshes = recs;
  t.meshCount = 2;
  pr.setMeshes(t);
  Matrix4 m;
  p.frameBegin();
  pr.beginFrame(Color32(0, 0, 0, 255));
  pr.drawMesh(0, m, 0);
  pr.drawMesh(1, m, 0);
  pr.drawMesh(1, m, 0);
  pr.endFrame();
  p.frameEnd();
  CHECK_EQ(p.peak().draws, 3u);
  CHECK_EQ(p.peak().triangles, 10u + 3u + 3u);
}
