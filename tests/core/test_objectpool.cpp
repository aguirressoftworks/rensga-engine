// F6.13 (A3, API 12): ObjectPool<T,N> and GameObjectPool<N>, Profiler samples, Budget queries, Application platform / profile.
#include "rensga/Budget.h"
#include "rensga/ObjectPool.h"
#include "rensga/Profiler.h"
#include "rensga/Rensga.h"
#include "rensga/core/application.h"
#include "rensga/core/runtime_stats.h"
#include "rensga/core/script_samples.h"
#include "rensga/core/scene_manager.h"
#include "rensga_test.h"
#include "scene_pack.h"

using namespace rensga;

namespace {
struct Bullet {
  i32 hp = 3;
  Scalar x = 0;
};
u64 g_clock = 0;
u64 fakeClock() { return g_clock; }
}  // namespace

TEST(object_pool_hands_out_capacity_items_then_null) {
  ObjectPool<Bullet, 4> pool;
  CHECK_EQ(pool.capacity(), 4u);
  Bullet* b[5];
  for (int i = 0; i < 4; ++i) {
    b[i] = pool.Get();
    CHECK(b[i] != nullptr);
  }
  CHECK(pool.Get() == nullptr);
  CHECK_EQ(pool.activeCount(), 4u);
  b[1]->hp = 99;
  CHECK(pool.Release(b[1]));
  CHECK(!pool.Release(b[1]));      // double release
  Bullet other;
  CHECK(!pool.Release(&other));    // not from this pool
  Bullet* again = pool.Get();
  CHECK(again == b[1] && again->hp == 3);  // reset on Get
  u32 n = 0;
  pool.ForEachActive([&](Bullet&) { ++n; });
  CHECK_EQ(n, 4u);
  pool.Reset();
  CHECK_EQ(pool.availableCount(), 4u);
}

TEST(game_object_pool_reuses_inactive_clones) {
  Arena arena;
  arena.create(1 << 20);
  World world;
  WorldConfig c;
  c.maxObjects = 32;
  c.maxComponentsPerType = 32;
  world.init(c, arena);
  GameObject* proto = world.createObject("Shot");
  proto->AddComponent<Light>();
  GameObjectPool<8> pool;
  CHECK_EQ(pool.Init(proto), 8u);
  CHECK_EQ(pool.availableCount(), 8u);
  const u32 objects = world.objectCount();
  GameObject* a = pool.Get(Vector3(1, 2, 3));
  CHECK(a != nullptr && a->activeSelf());
  CHECK_NEAR(a->transform.position().x, 1.0f, 1e-5f);
  GameObject* held[8];
  held[0] = a;
  for (int i = 1; i < 8; ++i) held[i] = pool.Get();
  CHECK(pool.Get() == nullptr);  // exhausted: no Instantiate behind the caller's back
  CHECK_EQ(world.objectCount(), objects);
  pool.Release(held[3]);
  CHECK(!held[3]->activeSelf());
  GameObject* reused = pool.Get(Vector3(9, 0, 0));
  CHECK(reused == held[3]);
  world.shutdown();
}

TEST(profiler_samples_sum_nest_and_report_last_frame) {
  script_samples::reset();
  script_samples::setClock(&fakeClock);
  g_clock = 1000;
  Profiler::BeginSample("AI");
  g_clock += 300;
  Profiler::BeginSample("Path");
  g_clock += 100;
  Profiler::EndSample();
  g_clock += 50;
  Profiler::EndSample();
  Profiler::BeginSample("AI");  // a second call in the same frame adds up
  g_clock += 20;
  Profiler::EndSample();
  script_samples::endFrame();
  CHECK_EQ(Profiler::LastSampleMicros("AI"), 470u);
  CHECK_EQ(Profiler::LastSampleMicros("Path"), 100u);
  CHECK_EQ(Profiler::LastSampleMicros("Nothing"), 0u);
  CHECK_EQ(script_samples::windowCount(), 2u);
  script_samples::WindowEntry e;
  CHECK(script_samples::windowEntry(0, &e));
  CHECK(strcmp(e.name, "AI") == 0 && e.calls == 2 && e.totalUs == 470 && e.maxFrameUs == 470 && e.frames == 1);
  {
    RENSGA_PROFILE_SCOPE("Scoped");
    g_clock += 7;
  }
  script_samples::endFrame();
  CHECK_EQ(Profiler::LastSampleMicros("Scoped"), 7u);
  CHECK_EQ(Profiler::LastSampleMicros("AI"), 0u);  // no AI work in the second frame
  Profiler::EndSample();                           // an unmatched EndSample is ignored
  script_samples::resetWindow();
  CHECK(script_samples::windowEntry(0, &e) && e.calls == 0);
  script_samples::setClock(nullptr);
  script_samples::reset();
}

TEST(profiler_names_are_limited_and_depth_is_capped) {
  script_samples::reset();
  script_samples::setClock(&fakeClock);
  static char names[40][8];
  for (int i = 0; i < 40; ++i) {
    snprintf(names[i], sizeof names[i], "n%d", i);
    Profiler::BeginSample(names[i]);
    Profiler::EndSample();
  }
  CHECK_EQ(script_samples::windowCount(), static_cast<u32>(script_samples::kMaxNames));
  for (u32 i = 0; i < script_samples::kMaxDepth + 3; ++i) Profiler::BeginSample("n0");
  for (u32 i = 0; i < script_samples::kMaxDepth + 3; ++i) Profiler::EndSample();
  script_samples::setClock(nullptr);
  script_samples::reset();
}

TEST(budget_reads_the_console_target_and_runtime_counters) {
  CHECK_EQ(Budget::memoryLimit(), 0u);  // no pack
  scenepack::Builder b;
  b.addScene("Main", {scenepack::Obj{"Root", -1, nullptr, 0, true}});
  b.ctgt.ramBytes = 24u << 20;
  b.ctgt.vramBytes = 2u << 20;
  b.ctgt.trianglesPerFrame = 20000;
  b.ctgt.audioVoices = 24;
  b.haveCtgt = true;
  size_t n = 0;
  u8* mem = b.build(&n);
  PackView view;
  CHECK(view.open(mem, n, "budget-test"));
  SceneManager::bind(&view, 0);
  CHECK_EQ(Budget::memoryLimit(), 24u << 20);
  CHECK_EQ(Budget::vramLimit(), 2u << 20);
  CHECK_EQ(Budget::trianglesLimit(), 20000u);
  CHECK_EQ(Budget::voicesLimit(), 24u);
  runtime_stats::setVram(1u << 20);
  runtime_stats::setTriangles(12345);
  CHECK_EQ(Budget::vramUsed(), 1u << 20);
  CHECK_EQ(Budget::trianglesLastFrame(), 12345u);
  CHECK_EQ(Budget::percentUsed(), 50u);
  AudioMixer mixer;
  runtime_stats::bindMixer(&mixer);
  CHECK_EQ(Budget::voicesUsed(), 0u);
  runtime_stats::reset();
  SceneManager::unbind();
  free(mem);
  CHECK_EQ(Budget::vramUsed() + Budget::trianglesLimit(), 0u);
}

TEST(application_platform_and_profile_queries) {
  Application::reset();
  Application::setPlatform(RuntimePlatform::Psp);
  CHECK(Application::platform() == RuntimePlatform::Psp);
  CHECK(Application::profile() == ConsoleProfile::Psp);  // implied by the console platform
  Application::setPlatform(RuntimePlatform::LinuxPlayer);
  CHECK(Application::profile() == ConsoleProfile::Unknown);
  Application::setProfile(ConsoleProfile::Ps2);  // a PC build of a PS2 project
  CHECK(Application::profile() == ConsoleProfile::Ps2);
  CHECK(strcmp(Application::profileName(Application::profile()), "PS2") == 0);
  CHECK(strcmp(Application::platformName(Application::platform()), "LinuxPlayer") == 0);
  Application::reset();
}
