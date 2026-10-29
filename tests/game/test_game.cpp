// MVP-29..33 acceptance: lifecycle order, physics, script registration, scripted-input gameplay, determinism.
#include <string.h>

#include "rensga/Sim.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

char g_lines[8192];
void captureLog(const LogRecord& r, void*) {
  if (strncmp(r.message, "rensga:", 7) != 0) return;
  size_t n = strlen(g_lines);
  snprintf(g_lines + n, sizeof(g_lines) - n, "%s\n", r.message);
}

struct LogCapture {
  CallbackSink sink{&captureLog, nullptr};
  LogCapture() { g_lines[0] = 0; log::addSink(&sink); }
  ~LogCapture() { log::removeSink(&sink); }
};

struct Sim {
  Arena arena;
  World world;
  Sim() {
    arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes);
    WorldConfig cfg;
    world.init(cfg, arena);
    defineDefaultInput();
  }
  ~Sim() { world.shutdown(); }
};

// ---- lifecycle -------------------------------------------------------------

char g_trace[256];
void tr(const char* s) { strncat(g_trace, s, sizeof(g_trace) - strlen(g_trace) - 1); }

struct TraceB : Behaviour {
  REFLECT(TraceB)
  void Awake() override { tr("Awake "); }
  void OnEnable() override { tr("OnEnable "); }
  void Start() override { tr("Start "); }
  void FixedUpdate() override { tr("Fixed "); }
  void Update() override { tr("Update "); }
  void LateUpdate() override { tr("Late "); }
  void OnDisable() override { tr("OnDisable "); }
  void OnDestroy() override { tr("OnDestroy "); }
  void OnTriggerEnter(GameObject*) override { tr("Trigger "); }
};

}  // namespace

TEST(api_lifecycle_unity_order) {
  Sim s;
  g_trace[0] = 0;
  GameObject* go = s.world.createObject("t");
  go->AddComponent<TraceB>();
  GameLoop loop;
  loop.init(s.world);
  VirtualGamepad pad;
  CHECK(loop.frame(1.0f / 60.0f, pad));
  CHECK(strcmp(g_trace, "Awake OnEnable Start Fixed Update Late ") == 0);
  g_trace[0] = 0;
  s.world.sendTriggerEnter(go, go);
  CHECK(strcmp(g_trace, "Trigger ") == 0);
  g_trace[0] = 0;
  go->SetActive(false);
  CHECK(strcmp(g_trace, "OnDisable ") == 0);
  g_trace[0] = 0;
  go->SetActive(true);
  CHECK(strcmp(g_trace, "OnEnable ") == 0);
  g_trace[0] = 0;
  Destroy(go);
  loop.frame(1.0f / 60.0f, pad);
  CHECK(strcmp(g_trace, "OnDisable OnDestroy ") == 0);
}

TEST(api_script_registration_by_name) {
  const TypeInfo* names[4] = {TypeRegistry::instance().find("PlayerController"), TypeRegistry::instance().find("FollowCamera"),
                              TypeRegistry::instance().find("Pickup"), TypeRegistry::instance().find("PickupCounter")};
  for (const TypeInfo* t : names) {
    CHECK(t != nullptr);
    CHECK(t && t->isBehaviour());
  }
  CHECK(names[0] && names[0]->fieldCount == 2);
  CHECK(names[1] && names[1]->fieldCount == 4);
  Sim s;
  GameObject* go = s.world.createObject("x");
  CHECK(names[2] && s.world.addComponent(go, *names[2]) != nullptr);
  CHECK(s.world.getComponent(go, *names[2]) != nullptr);
}

// ---- physics ---------------------------------------------------------------

TEST(physics_gravity_ground_and_wall) {
  Sim s;
  GameObject* floor = s.world.createObject("floor");
  floor->transform.localPosition = Vector3(0, -0.5f, 0);
  BoxCollider* fb = s.world.addComponent<BoxCollider>(floor);
  fb->size = Vector3(20, 1, 20);
  GameObject* wall = s.world.createObject("wall");
  wall->transform.localPosition = Vector3(3, 2, 0);
  BoxCollider* wb = s.world.addComponent<BoxCollider>(wall);
  wb->size = Vector3(1, 4, 4);
  GameObject* p = s.world.createObject("p");
  p->transform.localPosition = Vector3(0, 3, 0);
  CharacterController* cc = s.world.addComponent<CharacterController>(p);
  for (int i = 0; i < 120; ++i) cc->Move(Vector3(0.1f, 0, 0));
  CHECK(cc->isGrounded());
  CHECK_NEAR(p->transform.position().y, 1.0f, 0.01f);
  CHECK_NEAR(p->transform.position().x, 2.0f, 0.01f);  // wall face at x=2.5, radius 0.5
}

TEST(physics_sphere_trigger_fires_once) {
  Sim s;
  g_trace[0] = 0;
  GameObject* t = s.world.createObject("trig");
  t->transform.localPosition = Vector3(0, 1, 2);
  SphereCollider* sc = s.world.addComponent<SphereCollider>(t);
  sc->radius = 0.5f;
  sc->isTrigger = true;
  t->AddComponent<TraceB>();
  GameObject* p = s.world.createObject("p");
  p->transform.localPosition = Vector3(0, 1, 0);
  CharacterController* cc = s.world.addComponent<CharacterController>(p);
  cc->gravity = 0;
  GameLoop loop;
  loop.init(s.world);
  loop.setFixedStepHook(physics::fixedStep, nullptr);
  physics::resetTriggerMemory();
  VirtualGamepad pad;
  loop.frame(1.0f / 60.0f, pad);
  g_trace[0] = 0;
  p->transform.localPosition = Vector3(0, 1, 1.5f);
  for (int i = 0; i < 5; ++i) loop.frame(1.0f / 60.0f, pad);
  CHECK(strstr(g_trace, "Trigger ") != nullptr);
  CHECK(strstr(strstr(g_trace, "Trigger ") + 1, "Trigger ") == nullptr);
}

// ---- gameplay with scripted input -----------------------------------------

namespace {
void walkAndJump(InputScript& in) {
  in.add(0, 0, 1, 0);                              // forward: pickup 1 at z=4
  in.add(30, -1, 0, 0);                            // left toward Platform1 (x -5.5..-2.5)
  in.add(50, -1, 0, InputScript::South);           // jump while pushing into the platform
  in.add(55, -1, 0, 0);
  in.add(90, 0, 0, 0);
}
}  // namespace

TEST(game_walk_into_pickup_logs) {
  Sim s;
  LogCapture cap;
  CHECK(buildTestLevel(s.world));
  const u32 pickups = liveCount(s.world, "Pickup");
  CHECK_EQ(pickups, 5u);
  InputScript in;
  in.add(0, 0, 1, 0);
  in.add(60, 0, 0, 0);
  runSim(s.world, 90, &in, pickups);
  CHECK(strstr(g_lines, "rensga: pickup 1/5") != nullptr);
  CHECK(strstr(g_lines, "rensga: frame 60") != nullptr);
  CHECK_EQ(liveCount(s.world, "Pickup"), 4u);
}

TEST(game_jump_lands_on_box) {
  Sim s;
  CHECK(buildTestLevel(s.world));
  InputScript in;
  walkAndJump(in);
  runSim(s.world, 160, &in, 5);
  GameObject* player = s.world.findByName("Player");
  CHECK(player != nullptr);
  Vector3 pos = player->transform.position();
  CHECK_NEAR(pos.y, 2.0f, 0.02f);  // Platform1 top is y=1, controller half height 1
  CHECK(pos.x < -2.0f && pos.x > -5.5f);
  CHECK(player->GetComponent<CharacterController>()->isGrounded());
}

TEST(game_ramp_is_walkable) {
  Sim s;
  CHECK(buildTestLevel(s.world));
  GameObject* player = s.world.findByName("Player");
  player->transform.localPosition = Vector3(6, 1, -9);
  InputScript in;
  in.add(0, 0, 1, 0);  // up the ramp (+z), camera looks +z
  in.add(75, 0, 0, 0);
  runSim(s.world, 100, &in, 5);
  CHECK(player->transform.position().y > 2.1f);
}

TEST(game_camera_looks_at_player_and_orbits) {
  Sim s;
  CHECK(buildTestLevel(s.world));
  InputScript in;
  in.add(0, 0, 0, InputScript::R);
  runSim(s.world, 30, &in, 5);
  GameObject* cam = s.world.findByName("Camera");
  GameObject* player = s.world.findByName("Player");
  Matrix4 view = cam->transform.localToWorld().inverse();
  Vector3 v = view.transformPoint(player->transform.position());
  CHECK(v.z < -5.0f);                    // in front of the camera (-Z forward)
  CHECK(Mathf::Abs(v.x) < 0.1f);         // centred
  CHECK(cam->transform.position().x < -1.0f);  // orbited away from the start (-Z side)
}

TEST(game_two_identical_runs_same_hash) {
  u32 hashes[2];
  for (int run = 0; run < 2; ++run) {
    Sim s;
    CHECK(buildTestLevel(s.world));
    InputScript in;
    walkAndJump(in);
    hashes[run] = runSim(s.world, 200, &in, 5).hash;
  }
  CHECK_EQ(hashes[0], hashes[1]);
  Sim s;
  CHECK(buildTestLevel(s.world));
  CHECK(runSim(s.world, 200, nullptr, 5).hash != hashes[0]);  // different input, different state
}

TEST(game_no_heap_in_frames) {
  Sim s;
  CHECK(buildTestLevel(s.world));
  InputScript in;
  walkAndJump(in);
  GameLoop loop;
  loop.init(s.world);
  loop.setFixedStepHook(physics::fixedStep, nullptr);
  VirtualGamepad pad;
  bool ok = true;
  for (u32 f = 0; f < 200; ++f) {
    in.apply(f, pad);
    ok = loop.frame(1.0f / 60.0f, pad) && ok;
  }
  CHECK(ok);
}
