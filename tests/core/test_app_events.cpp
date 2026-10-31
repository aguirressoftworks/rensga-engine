// F5.13: injectApplicationPause queues OnApplicationPause events that pumpApplicationEvents delivers in order, with no platform
// bound (the dev-control 'apppause 0|1' command and the PSP emulator tests rely on this path).
#include "rensga/core/app_events.h"
#include "rensga/core/loop.h"
#include "rensga/core/system.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct PauseLog : Behaviour {
  REFLECT(PauseLog)
  i32 calls = 0;
  u32 bits = 0;  // the first 16 events, oldest in bit 0
  void OnApplicationPause(bool p) override {
    if (calls < 16 && p) bits |= 1u << calls;
    ++calls;
  }
};
struct AppEventsWorld {  // a small world
  Arena arena;
  World world;
  AppEventsWorld() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
  }
};
}  // namespace

TEST(inject_application_pause_is_delivered_in_order) {
  system_services::bind(nullptr);
  AppEventsWorld f;
  GameObject* go = f.world.createObject("p");
  PauseLog* pl = go->AddComponent<PauseLog>();
  GameLoop loop;
  loop.init(f.world);
  VirtualGamepad pad;
  loop.frame(1.0f / 60.0f, pad);  // Start
  const u32 resumes0 = applicationResumeCount();
  pumpApplicationEvents(f.world);
  CHECK_EQ(pl->calls, 0);
  injectApplicationPause(true);
  injectApplicationPause(false);
  CHECK_EQ(pl->calls, 0);  // queued, not delivered yet
  pumpApplicationEvents(f.world);
  CHECK_EQ(pl->calls, 2);
  CHECK_EQ(pl->bits, 1u);  // true, then false
  CHECK_EQ(applicationResumeCount(), resumes0 + 1);
  pumpApplicationEvents(f.world);
  CHECK_EQ(pl->calls, 2);  // the queue is empty again
}

TEST(inject_application_pause_queue_drops_the_oldest) {
  system_services::bind(nullptr);
  AppEventsWorld f;
  GameObject* go = f.world.createObject("p");
  PauseLog* pl = go->AddComponent<PauseLog>();
  GameLoop loop;
  loop.init(f.world);
  VirtualGamepad pad;
  loop.frame(1.0f / 60.0f, pad);
  for (int i = 0; i < 10; ++i) injectApplicationPause(i == 0);  // one true, then nine false: the queue keeps 8
  pumpApplicationEvents(f.world);
  CHECK_EQ(pl->calls, 8);
  CHECK_EQ(pl->bits, 0u);  // the leading true was the oldest and is gone
}
