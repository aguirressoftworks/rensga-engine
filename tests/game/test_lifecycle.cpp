// API 3 exercises: the lifecycle order test and the projectile spawner. The expected sequences follow
// Unity's documented order: Awake, OnEnable, Start (before the first Update), FixedUpdate, Update,
// LateUpdate, OnDisable, OnDestroy. Objects created during a frame get Awake and OnEnable at once.
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

char g_seq[1024];
void rec(const char* who, const char* what) {
  size_t n = strlen(g_seq);
  snprintf(g_seq + n, sizeof(g_seq) - n, "%s%s.%s", n ? " " : "", who, what);
}
bool seqIs(const char* expected) { return strcmp(g_seq, expected) == 0; }
void expectSeq(const char* expected, int line) {
  if (!seqIs(expected)) {
    fprintf(stderr, "  line %d\n    expected: %s\n    actual:   %s\n", line, expected, g_seq);
    ++rtest::failures();
  }
  g_seq[0] = 0;
}
#define EXPECT_SEQ(s) expectSeq(s, __LINE__)

#define LC_BODY(NAME)                                   \
  void Awake() override { rec(NAME, "Awake"); }          \
  void OnEnable() override { rec(NAME, "OnEnable"); }    \
  void Start() override { rec(NAME, "Start"); }          \
  void FixedUpdate() override { rec(NAME, "Fixed"); }    \
  void Update() override { rec(NAME, "Update"); }        \
  void LateUpdate() override { rec(NAME, "Late"); }      \
  void OnDisable() override { rec(NAME, "OnDisable"); }  \
  void OnDestroy() override { rec(NAME, "OnDestroy"); }

struct LcA : Behaviour {
  REFLECT(LcA)
  LC_BODY("A")
};
// Runs before every script that keeps the default order 0.
struct LcEarly : Behaviour {
  REFLECT(LcEarly)
  EXECUTION_ORDER(-10)
  LC_BODY("E")
};
struct LcLate : Behaviour {
  REFLECT(LcLate)
  EXECUTION_ORDER(100)
  LC_BODY("L")
};

// Instantiates a template from Update, once.
struct LcSpawner : Behaviour {
  REFLECT(LcSpawner)
  GameObject* templ = nullptr;
  bool done = false;
  bool armed = false;
  void Update() override {
    if (done || !armed || !templ) return;
    done = true;
    rec("S", "Update-begin");
    Instantiate(templ);
    rec("S", "Update-end");
  }
};

struct Rig {
  Arena arena;
  World world;
  GameLoop loop;
  VirtualGamepad pad;
  Rig() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    world.init(c, arena);
    loop.init(world);
    g_seq[0] = 0;
  }
  ~Rig() {
    g_seq[0] = 0;
    world.shutdown();
  }
  void frame() { loop.frame(1.0f / 60.0f, pad); }
};

}  // namespace

TEST(lifecycle_unity_order) {
  Rig r;
  GameObject* go = r.world.createObject("probe");
  go->AddComponent<LcA>();
  // Nothing runs until the first frame (scene load: callbacks start with the loop).
  EXPECT_SEQ("");
  r.frame();
  EXPECT_SEQ("A.Awake A.OnEnable A.Start A.Fixed A.Update A.Late");
  r.frame();
  EXPECT_SEQ("A.Fixed A.Update A.Late");

  // SetActive(false) -> OnDisable now; no ticks; SetActive(true) -> OnEnable only (no second Awake or Start).
  go->SetActive(false);
  EXPECT_SEQ("A.OnDisable");
  r.frame();
  EXPECT_SEQ("");
  go->SetActive(true);
  EXPECT_SEQ("A.OnEnable");
  r.frame();
  EXPECT_SEQ("A.Fixed A.Update A.Late");

  // Disabling just the component behaves the same way.
  go->GetComponent<LcA>()->setEnabled(false);
  EXPECT_SEQ("A.OnDisable");
  go->GetComponent<LcA>()->setEnabled(true);
  EXPECT_SEQ("A.OnEnable");

  // Destroy: references go null at once, OnDisable then OnDestroy at the end of the frame.
  Ref<GameObject> ref = go->ref();
  Destroy(go);
  CHECK(ref.get() == nullptr);
  EXPECT_SEQ("");
  r.frame();
  EXPECT_SEQ("A.OnDisable A.OnDestroy");
  r.frame();
  EXPECT_SEQ("");
}

TEST(lifecycle_inactive_object_wakes_on_activation) {
  Rig r;
  GameObject* go = r.world.createObject("sleeper");
  go->AddComponent<LcA>();
  go->SetActive(false);
  r.frame();
  r.frame();
  EXPECT_SEQ("");  // an inactive object never gets Awake
  go->SetActive(true);
  EXPECT_SEQ("A.Awake A.OnEnable");  // Awake and OnEnable at the moment of activation
  r.frame();
  EXPECT_SEQ("A.Start A.Fixed A.Update A.Late");  // Start before its first Update
}

TEST(lifecycle_instantiate_during_update) {
  Rig r;
  GameObject* templ = r.world.createObject("templ");
  templ->AddComponent<LcA>();
  LcSpawner* sp = r.world.createObject("host")->AddComponent<LcSpawner>();
  sp->templ = templ;
  r.frame();
  EXPECT_SEQ("A.Awake A.OnEnable A.Start A.Fixed A.Update A.Late");  // the template itself, nothing spawned yet
  sp->armed = true;
  r.frame();
  // The clone wakes up inside Instantiate, between the spawner's own markers. Its Start has not run, so it
  // takes no part in this frame's remaining callbacks.
  EXPECT_SEQ("A.Fixed A.Update S.Update-begin A.Awake A.OnEnable S.Update-end A.Late");
  r.frame();
  EXPECT_SEQ("A.Start A.Fixed A.Fixed A.Update A.Update A.Late A.Late");  // Start before the clone's first Update
}

TEST(lifecycle_script_execution_order) {
  Rig r;
  GameObject* go = r.world.createObject("order");
  // Added in the "wrong" order on purpose: the execution order, not the add order, decides.
  go->AddComponent<LcLate>();
  go->AddComponent<LcA>();
  go->AddComponent<LcEarly>();
  r.frame();
  EXPECT_SEQ(
      "E.Awake E.OnEnable A.Awake A.OnEnable L.Awake L.OnEnable "
      "E.Start A.Start L.Start "
      "E.Fixed A.Fixed L.Fixed "
      "E.Update A.Update L.Update "
      "E.Late A.Late L.Late");
  CHECK_EQ(LcEarly::staticTypeInfo()->executionOrder, -10);
  CHECK_EQ(LcA::staticTypeInfo()->executionOrder, 0);
  CHECK_EQ(LcLate::staticTypeInfo()->executionOrder, 100);
}

// ---- projectile spawner exercise ------------------------------------------------------------------

namespace {

struct LcProjectile : Behaviour {
  REFLECT(LcProjectile)
  SERIALIZE_FIELD(Scalar, speed, 10)
  SERIALIZE_FIELD(Scalar, lifetime, 0.5f)
  void Start() override { Destroy(gameObject(), lifetime); }
  void Update() override { transform()->Translate(Vector3(0, 0, speed * Time::deltaTime()), Space::World); }
};

}  // namespace

TEST(exercise_projectile_spawner) {
  Rig r;
  const u32 kPool = 4;  // the template itself uses one slot
  CHECK(r.world.reservePool(*LcProjectile::staticTypeInfo(), kPool));
  GameObject* templ = r.world.createObject("Projectile");
  templ->AddComponent<LcProjectile>();
  templ->SetActive(false);

  Ref<GameObject> shots[6];
  u32 spawned = 0;
  for (int i = 0; i < 6; ++i) {
    GameObject* p = Instantiate(templ, Vector3(0, 0, static_cast<Scalar>(i)), Quaternion::identity());
    if (!p) continue;
    p->SetActive(true);
    shots[spawned++] = p->ref();
  }
  CHECK_EQ(spawned, kPool - 1);  // pool capacity respected: the extra shots are refused, not crashed
  r.frame();
  r.frame();
  for (u32 i = 0; i < spawned; ++i) CHECK(shots[i].get() != nullptr);
  CHECK(shots[0]->transform.position().z > 0);  // moving
  for (int i = 0; i < 40; ++i) r.frame();         // 0.5 s lifetime passed
  for (u32 i = 0; i < spawned; ++i) CHECK(shots[i].get() == nullptr);  // Refs read null after Destroy
  CHECK_EQ(r.world.pool<LcProjectile>()->liveCount, 1u);               // only the template is left
  GameObject* again = Instantiate(templ);  // freed slots are reusable
  CHECK(again != nullptr);
  CHECK(shots[0].get() == nullptr);        // and a stale Ref stays null
}
