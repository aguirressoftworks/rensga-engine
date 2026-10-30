// F3.05 exercise "coroutine door": a Door opens over 2 s with a coroutine and fires OnOpened (a UnityEvent in the
// scene JSON that targets another component's METHOD). Cooked, loaded and run headless.
#include <stdio.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga/core/unity_event.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

struct Door : Behaviour {
  REFLECT(Door)
  SERIALIZE_FIELD(Scalar, openSeconds, 1)
  SERIALIZE_FIELD(UnityEvent, OnOpened, {})
  Scalar opened = 0;  // 0..1
  void Start() override {
    const Scalar dur = openSeconds;
    StartCoroutine([this, dur](CoroutineContext& co) -> Yield {
      if (co.step == 0) { ++co.step; return YieldNull(); }
      opened += Time::deltaTime() / dur;
      if (opened < 1) return YieldNull();
      opened = 1;
      OnOpened.Invoke();
      return YieldBreak();
    });
  }
};
struct DoorBell : Behaviour {
  REFLECT(DoorBell)
  METHOD(Ring)
  METHOD(SetVolume)
  i32 rings = 0;
  Scalar ringTime = -1;
  Scalar volume = 0;
  void Ring() { ++rings; ringTime = Time::time(); }
  void SetVolume(Scalar v) { volume = v; }
};

static CookResult cookDoor(const char* fixture) {
  CookOptions o;
  o.project = fs::path(RENSGA_FIXTURES_DIR) / fixture;
  o.force = true;
  o.genHeader = o.project / "Library" / "Generated" / "console_target.gen.h";
  return cookProject(o, [](const CookMessage& m) { fprintf(stderr, "    [cook] %s\n", m.text.c_str()); });
}

TEST(door_event_fires_once_at_two_seconds_from_a_cooked_pack) {
  CookResult r = cookDoor("door");
  CHECK(r.ok);
  std::ifstream in(r.packPath, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Arena packMem;
  packMem.create(bytes.size() + 64);
  u8* data = static_cast<u8*>(packMem.allocate(bytes.size()));
  memcpy(data, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(data, bytes.size()));

  Arena arena;
  arena.create(1 << 20);
  World world;
  WorldConfig cfg;
  cfg.maxObjects = 16;
  cfg.maxComponentsPerType = 8;
  CHECK(world.init(cfg, arena));
  LoadedPack lp;
  CHECK(world.loadPack(view, &lp));
  CHECK_EQ(world.scheduler().coroutineCapacity(), 4u);  // ConsoleTarget budgets.maxCoroutines reached the runtime

  GameLoop loop;
  loop.init(world);
  VirtualGamepad pad;
  DoorBell* bell = world.findByName("Bell")->GetComponent<DoorBell>();
  heap::checkReset();
  for (int i = 0; i < 59; ++i) loop.frame(1.0f / 60.0f, pad);
  CHECK_EQ(bell->rings, 0);
  for (int i = 0; i < 90; ++i) loop.frame(1.0f / 60.0f, pad);
  CHECK_EQ(bell->rings, 1);
  CHECK(bell->ringTime > 1.9f && bell->ringTime < 2.2f);
  CHECK_NEAR(bell->volume, 0.75f, 1e-5f);
  CHECK_EQ(heap::checkAllocs(), 0u);
  world.shutdown();
}

TEST(cooker_rejects_unity_event_method_not_declared_with_METHOD) {
  CookResult r = cookDoor("door_bad");
  CHECK(!r.ok);
  bool named = false;
  for (const CookMessage& m : r.errors) named = named || m.text.find("NotDeclared") != std::string::npos;
  CHECK(named);
}
