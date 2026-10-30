// F3.05 (API 4): coroutines, Invoke, Event<Args>, UnityEvent, method registry.
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga/core/event.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/unity_event.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

struct CoFixture {
  Arena arena;
  World world;
  GameLoop loop;
  VirtualGamepad pad;
  explicit CoFixture(u32 maxCo = 8) {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    c.maxCoroutines = maxCo;
    world.init(c, arena);
    Time::setTimeScale(1.0f);
    loop.init(world);
  }
  ~CoFixture() { Time::setTimeScale(1.0f); world.shutdown(); }
  void frames(int n) {
    for (int i = 0; i < n; ++i) loop.frame(1.0f / 60.0f, pad);
  }
};

struct CoProbe : Behaviour {
  REFLECT(CoProbe)
  SERIALIZE_FIELD(i32, count, 0)
  SERIALIZE_FIELD(Scalar, level, 0)
  SERIALIZE_FIELD(bool, flag, false)
  SERIALIZE_FIELD(UnityEvent, OnDone, {})
  METHOD(Bump)
  METHOD(SetLevel)
  METHOD(SetCount)
  METHOD(SetFlag)
  i32 order[8] = {};
  i32 orderN = 0;
  i32 fixedSeen = 0, endSeen = 0;
  Scalar firedAt = -1;
  void Bump() { ++count; }
  void SetLevel(Scalar v) { level = v; }
  void SetCount(i32 v) { count = v; }
  void SetFlag(bool v) { flag = v; }
  void Mark(i32 v) { if (orderN < 8) order[orderN++] = v; }
  void Stamp() { firedAt = Time::time(); ++count; }
};

Guid mkGuid(u8 n) {
  Guid g;
  g.bytes[0] = 0xAB;
  g.bytes[15] = n;
  return g;
}

CoProbe* addProbe(CoFixture& f, const char* name = "P") {
  GameObject* go = f.world.createObject(name);
  return go->AddComponent<CoProbe>();
}

// capture: this + a number, fits the inline buffer
Yield stepBody(CoroutineContext& co, CoProbe* p) {
  switch (co.step++) {
    case 0: return WaitForSeconds(0.5f);
    case 1: p->Bump(); return YieldNull();
    case 2: p->Bump(); return YieldBreak();
    default: return YieldBreak();
  }
}

}  // namespace

TEST(coroutine_step_function_waits_scaled_seconds) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  Coroutine c = p->StartCoroutine([p](CoroutineContext& co) { return stepBody(co, p); });
  CHECK(c.isValid() && c.isRunning());
  f.frames(20);  // 0.33 s
  CHECK_EQ(p->count, 0);
  f.frames(14);  // past 0.5 s
  CHECK(p->count >= 1);  // the wait ended around frame 30; the second Bump follows one frame later
  f.frames(2);
  CHECK_EQ(p->count, 2);
  CHECK(!c.isRunning());
  CHECK_EQ(f.world.scheduler().coroutineCount(), 0u);
}

TEST(coroutine_time_scale_slows_wait_but_not_realtime) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  Time::setTimeScale(0.5f);
  p->StartCoroutine(Wait(1.0f).Then([p] { p->Mark(1); }));
  p->StartCoroutine(Chain().WaitRealtime(1.0f).Then([p] { p->Mark(2); }));
  f.frames(62);
  CHECK_EQ(p->orderN, 1);
  CHECK_EQ(p->order[0], 2);  // realtime fired at ~1 s, scaled one still waiting
  f.frames(62);
  CHECK_EQ(p->orderN, 2);
  CHECK_EQ(p->order[1], 1);
}

TEST(coroutine_chain_runs_in_order) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  p->StartCoroutine(Wait(0.1f).Then([p] { p->Mark(1); }).WaitUntil([p] { return p->flag; }).Then([p] { p->Mark(2); }).NextFrame().Then([p] { p->Mark(3); }));
  f.frames(10);
  CHECK_EQ(p->orderN, 1);
  CHECK_EQ(p->order[0], 1);
  p->flag = true;
  f.frames(1);
  CHECK_EQ(p->orderN, 2);
  CHECK_EQ(p->order[1], 2);
  f.frames(2);
  CHECK_EQ(p->orderN, 3);
  CHECK_EQ(p->order[2], 3);
}

TEST(coroutine_wait_while_and_fixed_and_end_of_frame) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  p->flag = true;
  p->StartCoroutine(Chain().WaitWhile([p] { return p->flag; }).Then([p] { p->Mark(1); }));
  p->StartCoroutine(Chain().WaitFixedUpdate().Then([p] { ++p->fixedSeen; }));
  p->StartCoroutine(Chain().WaitEndOfFrame().Then([p] { ++p->endSeen; }));
  f.frames(3);
  CHECK_EQ(p->orderN, 0);
  CHECK_EQ(p->fixedSeen, 1);
  CHECK_EQ(p->endSeen, 1);
  p->flag = false;
  f.frames(1);
  CHECK_EQ(p->orderN, 1);
}

TEST(coroutine_stop_and_stop_all) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  Coroutine a = p->StartCoroutine(Wait(1).Then([p] { p->Bump(); }));
  Coroutine b = p->StartCoroutine(Wait(1).Then([p] { p->Bump(); }));
  p->StopCoroutine(a);
  CHECK(!a.isRunning() && b.isRunning());
  p->StartCoroutine(Wait(1).Then([p] { p->Bump(); }));
  p->StopAllCoroutines();
  CHECK(!b.isRunning());
  f.frames(90);
  CHECK_EQ(p->count, 0);
}

TEST(coroutine_stops_when_owner_destroyed_or_deactivated) {
  CoFixture f;
  CoProbe* p = addProbe(f, "A");
  CoProbe* q = addProbe(f, "B");
  CoProbe* keep = addProbe(f, "C");
  p->StartCoroutine(Wait(0.5f).Then([p] { p->Bump(); }));
  q->StartCoroutine(Wait(0.5f).Then([q] { q->Bump(); }));
  keep->StartCoroutine(Wait(0.5f).Then([keep] { keep->Bump(); }));
  p->gameObject()->SetActive(false);
  Destroy(q->gameObject());
  f.frames(40);
  CHECK_EQ(keep->count, 1);
  CHECK_EQ(p->count, 0);
  p->gameObject()->SetActive(true);  // does not resume (Unity)
  f.frames(40);
  CHECK_EQ(p->count, 0);
  CHECK_EQ(f.world.scheduler().coroutineCount(), 0u);
}

TEST(coroutine_pool_is_fixed_and_full_pool_refuses) {
  CoFixture f(2);
  CoProbe* p = addProbe(f);
  Coroutine a = p->StartCoroutine(Wait(1));
  Coroutine b = p->StartCoroutine(Wait(1));
  Coroutine c = p->StartCoroutine(Wait(1));
  CHECK(a.isValid() && b.isValid() && !c.isValid());
  CHECK_EQ(f.world.scheduler().coroutineCapacity(), 2u);
  p->StopCoroutine(a);
  CHECK(p->StartCoroutine(Wait(1)).isValid());
}

TEST(coroutine_handle_generation_survives_slot_reuse) {
  CoFixture f(1);
  CoProbe* p = addProbe(f);
  Coroutine a = p->StartCoroutine(Wait(1));
  p->StopCoroutine(a);
  Coroutine b = p->StartCoroutine(Wait(1));
  CHECK(!a.isRunning() && b.isRunning());
  p->StopCoroutine(a);  // stale handle must not stop b
  CHECK(b.isRunning());
}

TEST(invoke_once_repeating_cancel_and_by_name) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  p->Invoke(&CoProbe::Bump, 0.5f);
  CHECK(p->IsInvoking(&CoProbe::Bump));
  f.frames(20);
  CHECK_EQ(p->count, 0);
  f.frames(14);
  CHECK_EQ(p->count, 1);
  CHECK(!p->IsInvoking(&CoProbe::Bump));

  p->count = 0;
  p->InvokeRepeating(&CoProbe::Bump, 0.1f, 0.25f);
  f.frames(60);  // 1 s: fires at 0.1, 0.35, 0.6, 0.85
  CHECK(p->count == 4 || p->count == 3);
  p->CancelInvoke(&CoProbe::Bump);
  CHECK(!p->IsInvoking());
  i32 before = p->count;
  f.frames(60);
  CHECK_EQ(p->count, before);

  p->count = 0;
  p->Invoke("Bump", 0.2f);   // by name through METHOD(Bump)
  CHECK(p->IsInvoking("Bump"));
  p->CancelInvoke("Bump");
  f.frames(30);
  CHECK_EQ(p->count, 0);
  p->Invoke("Bump", 0.2f);
  f.frames(30);
  CHECK_EQ(p->count, 1);
  p->Invoke("Nope", 0.1f);  // unknown: warns, no crash
}

TEST(invoke_stops_with_owner) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  CoProbe* q = addProbe(f, "Q");
  p->Invoke(&CoProbe::Bump, 0.3f);
  q->Invoke(&CoProbe::Bump, 0.3f);
  Destroy(p->gameObject());
  f.frames(30);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(f.world.scheduler().invokeCount(), 0u);
}

TEST(coroutine_and_invoke_do_not_allocate_per_frame) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  heap::checkReset();
  f.loop.frame(1.0f / 60.0f, f.pad);
  for (int i = 0; i < 200; ++i) {
    if (i % 10 == 0) {
      p->StartCoroutine(Wait(0.05f).Then([p] { p->Bump(); }).WaitUntil([p] { return p->count > 0; }));
      p->Invoke(&CoProbe::Bump, 0.03f);
      p->InvokeRepeating("Bump", 0.02f, 0.05f);
    }
    if (i % 25 == 0) p->CancelInvoke();
    CHECK(f.loop.frame(1.0f / 60.0f, f.pad));
  }
  CHECK_EQ(heap::checkAllocs(), 0u);
}

// ---- Event<Args> -------------------------------------------------------------------------------------------------

namespace {
i32 g_free = 0;
void freeHandler(i32 v) { g_free += v; }
Event<i32>* g_selfRemoving = nullptr;
void selfRemoving(i32) { ++g_free; g_selfRemoving->Unsubscribe(&selfRemoving); }
struct EvProbe : Behaviour {
  REFLECT(EvProbe)
  i32 got = 0;
  void On(i32 v) { got += v; }
};
}  // namespace

TEST(event_subscribe_unsubscribe_and_order) {
  g_free = 0;
  Event<i32> e;
  e.Subscribe(&freeHandler);
  e += [](i32 v) { g_free += v * 10; };
  e.Invoke(2);
  CHECK_EQ(g_free, 22);
  e -= &freeHandler;
  e.Invoke(1);
  CHECK_EQ(g_free, 32);
  CHECK_EQ(e.Count(), 1u);
  e.Clear();
  e.Invoke(5);
  CHECK_EQ(g_free, 32);
}

TEST(event_handler_may_unsubscribe_during_invoke) {
  g_free = 0;
  Event<i32> e;
  g_selfRemoving = &e;
  e.Subscribe(&selfRemoving);
  e.Subscribe(&freeHandler);
  e.Invoke(3);
  CHECK_EQ(g_free, 4);  // both ran once
  e.Invoke(3);
  CHECK_EQ(g_free, 7);  // only freeHandler
  CHECK_EQ(e.Count(), 1u);
}

TEST(event_member_handlers_and_destroyed_owner_skipped) {
  CoFixture f;
  GameObject* a = f.world.createObject("A");
  GameObject* b = f.world.createObject("B");
  EvProbe* pa = a->AddComponent<EvProbe>();
  EvProbe* pb = b->AddComponent<EvProbe>();
  Event<i32> e;
  CHECK(e.Subscribe(&EvProbe::On, pa));
  CHECK(e.Subscribe(&EvProbe::On, pb));
  e.Invoke(1);
  CHECK_EQ(pa->got, 1);
  CHECK_EQ(pb->got, 1);
  Destroy(b);
  f.frames(2);
  e.Invoke(1);
  CHECK_EQ(pa->got, 2);
  CHECK_EQ(e.Count(), 1u);
  CHECK(e.Unsubscribe(&EvProbe::On, pa));
  CHECK_EQ(e.Count(), 0u);
}

TEST(event_capacity_is_fixed) {
  Event<> e;
  int ok = 0;
  for (u32 i = 0; i < Event<>::kCapacity + 3; ++i) ok += e.Subscribe(+[]() {}) ? 1 : 0;
  CHECK_EQ(ok, static_cast<int>(Event<>::kCapacity));
}

// ---- method registry + UnityEvent -------------------------------------------------------------------------------

TEST(method_registry_lists_declared_methods) {
  const TypeInfo* t = TypeRegistry::instance().find("CoProbe");
  CHECK(t != nullptr);
  std::vector<const MethodInfo*> m = methodsOf(*t);
  CHECK_EQ(m.size(), 4u);
  const MethodInfo* sl = findMethod(*t, "SetLevel");
  CHECK(sl && sl->arg == MethodArgKind::Scalar);
  CHECK(findMethod(*t, "SetCount")->arg == MethodArgKind::I32);
  CHECK(findMethod(*t, "SetFlag")->arg == MethodArgKind::Bool);
  CHECK(findMethod(*t, "Bump")->arg == MethodArgKind::None);
  CHECK(findMethod(*t, "Mark") == nullptr);  // not declared with METHOD
  forgetType(nullptr);
}

TEST(unity_event_invokes_targets_by_guid) {
  CoFixture f;
  CoProbe* src = addProbe(f, "Src");
  CoProbe* dst = addProbe(f, "Dst");
  dst->gameObject()->guid = mkGuid(1);
  src->OnDone.AddListener(dst->gameObject()->guid, "CoProbe", "Bump");
  src->OnDone.AddListener(dst->gameObject()->guid, "CoProbe", "SetLevel")->setScalar(2.5f);
  src->OnDone.AddListener(dst->gameObject()->guid, "CoProbe", "SetFlag")->setBool(true);
  src->OnDone.AddListener(mkGuid(99), "CoProbe", "Bump");  // missing target: skipped
  CHECK_EQ(src->OnDone.Invoke(), 3u);
  CHECK_EQ(dst->count, 1);
  CHECK_NEAR(dst->level, 2.5f, 1e-5f);
  CHECK(dst->flag);
  Destroy(dst->gameObject());
  f.frames(2);
  CHECK_EQ(src->OnDone.Invoke(), 0u);
}

TEST(unity_event_json_round_trip_and_errors) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  const TypeInfo* t = CoProbe::staticTypeInfo();
  Guid g = mkGuid(7);
  p->OnDone.AddListener(g, "CoProbe", "SetLevel")->setScalar(1.5f);
  p->OnDone.AddListener(g, "CoProbe", "SetCount")->setI32(4);
  nlohmann::json j = fieldsToJson(*t, p);
  CHECK(j["OnDone"].is_array() && j["OnDone"].size() == 2);
  CHECK(j["OnDone"][0]["method"] == "SetLevel");
  CHECK(j["OnDone"][1]["arg"] == 4);
  CoProbe* q = addProbe(f, "Q");
  std::string err;
  CHECK(fieldsFromJson(*t, q, j, &err));
  CHECK(fieldsToJson(*t, q) == j);
  CHECK_EQ(q->OnDone.count, 2u);
  nlohmann::json bad = {{"OnDone", {{{"target", "nope"}, {"type", "CoProbe"}, {"method", "Bump"}}}}};
  CHECK(!fieldsFromJson(*t, q, bad, &err));
  CHECK(err.find("OnDone") != std::string::npos);
  nlohmann::json empty = {{"OnDone", nlohmann::json::array()}};
  CHECK(fieldsFromJson(*t, q, empty, &err));
  CHECK_EQ(q->OnDone.count, 0u);
}

TEST(unity_event_blob_round_trip) {
  CoFixture f;
  CoProbe* p = addProbe(f);
  const TypeInfo* t = CoProbe::staticTypeInfo();
  p->OnDone.AddListener(mkGuid(3), "CoProbe", "SetCount")->setI32(9);
  u8 buf[1024];
  u32 n = saveState(*t, p, buf, sizeof(buf));
  CHECK(n > 0);
  CoProbe* q = addProbe(f, "Q");
  CHECK(loadState(*t, q, buf, n) >= 0);
  u8 blob[1024];
  CHECK(blobSize(*t) <= sizeof(blob));
  writeBlob(*t, p, blob);
  CoProbe* r = addProbe(f, "R");
  CHECK(readBlob(*t, r, blob, blobSize(*t)));
  CHECK(fieldsEqual(*t, p, r));
  CHECK_EQ(q->OnDone.count, 1u);
  CHECK(q->OnDone.listeners[0].decode().i == 9);
}
