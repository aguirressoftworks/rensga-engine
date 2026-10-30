#include "rensga/Rensga.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct LoopFixture {
  Arena arena;
  World world;
  GameLoop loop;
  explicit LoopFixture(const LoopConfig& cfg) {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
    Time::setTimeScale(1.0f);
    loop.init(world, cfg);
  }
  ~LoopFixture() { Time::setTimeScale(1.0f); }
};
}  // namespace

TEST(loop_50hz_gives_50_steps_per_second) {
  LoopConfig cfg = LoopConfig::forRefreshHz(50);
  CHECK_EQ(cfg.refreshHz, 50u);
  LoopFixture f(cfg);
  VirtualGamepad pad;
  for (int i = 0; i < 50; ++i) f.loop.frame(1.0f / 50.0f, pad);
  CHECK_EQ(f.loop.fixedStepCount(), 50u);
  CHECK_EQ(Time::frameCount(), 50u);
  CHECK_NEAR(Time::fixedDeltaTime(), 1.0f / 50.0f, 1e-6f);
  CHECK_NEAR(Time::fixedTime(), 1.0f, 1e-3f);
  CHECK_NEAR(Time::time(), 1.0f, 1e-3f);
}

TEST(loop_time_scale_half) {
  LoopFixture f(LoopConfig::forRefreshHz(50));
  Time::setTimeScale(0.5f);
  VirtualGamepad pad;
  for (int i = 0; i < 50; ++i) f.loop.frame(1.0f / 50.0f, pad);
  CHECK_EQ(f.loop.fixedStepCount(), 25u);
  CHECK_NEAR(Time::deltaTime(), 0.01f, 1e-6f);
  CHECK_NEAR(Time::unscaledDeltaTime(), 0.02f, 1e-6f);
  CHECK_NEAR(Time::time(), 0.5f, 1e-3f);
  CHECK_NEAR(Time::unscaledTime(), 1.0f, 1e-3f);
  Time::setTimeScale(-3.0f);
  CHECK_EQ(Time::timeScale(), 0.0f);
}

TEST(loop_interpolation_is_leftover_fraction) {
  LoopFixture f(LoopConfig::forRefreshHz(50));
  VirtualGamepad pad;
  f.loop.frame(0.03f, pad);  // one 0.02 step, 0.01 left
  CHECK_EQ(f.loop.fixedStepCount(), 1u);
  CHECK_NEAR(Time::interpolation(), 0.5f, 1e-3f);
  CHECK_NEAR(f.loop.interpolation(), Time::interpolation(), 1e-6f);
}

TEST(loop_clean_frame_reports_zero_allocs) {
  LoopFixture f{LoopConfig()};
  VirtualGamepad pad;
  heap::checkReset();
  f.loop.frame(1.0f / 60.0f, pad);  // frame 1 sets the baseline
  for (int i = 0; i < 10; ++i) CHECK(f.loop.frame(1.0f / 60.0f, pad));
  CHECK_EQ(heap::checkFrames(), 11u);
  CHECK_EQ(heap::checkAllocs(), 0u);
  CHECK_EQ(heap::logAllocCheck(), 0u);
}

TEST(alloc_check_detects_allocation_after_first_frame) {
  LoopFixture f{LoopConfig()};
  VirtualGamepad pad;
  heap::checkReset();
  int* early = new int(1);  // before the first frame ends: not counted
  f.loop.frame(1.0f / 60.0f, pad);
  delete early;
  CHECK_EQ(heap::checkAllocs(), 0u);
  int* late = new int(2);
  f.loop.frame(1.0f / 60.0f, pad);
  delete late;
  CHECK_EQ(heap::checkAllocs(), 1u);
  CHECK_EQ(heap::logAllocCheck(), 1u);
  heap::checkReset();
}

namespace {
struct QuadCounter : NullRenderer {
  u32 quads = 0;
  void drawOverlayQuad(i32, i32, i32, i32, Color32) override { ++quads; }
};
}  // namespace

TEST(memory_overlay_draws_bars_only_when_enabled) {
  MemoryBudget b;
  b.init(1 << 20, 0);
  Arena a;
  CHECK(b.createArena(a, "world", 1 << 16));
  a.allocate(1 << 12);
  QuadCounter r;
  setDebugOverlay(false);
  drawMemoryOverlay(r, b);
  CHECK_EQ(r.quads, 0u);
  setDebugOverlay(true);
  drawMemoryOverlay(r, b);
  CHECK(r.quads >= 5u);  // panel + arena track/fill/peak + frame track/fill/tick
  setDebugOverlay(false);
}
