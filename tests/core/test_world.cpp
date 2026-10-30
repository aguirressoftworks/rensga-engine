#include <string.h>

#include "rensga/Rensga.h"
#include "rensga_test.h"

using namespace rensga;

static char g_trace[512];
static void trace(const char* s) {
  strncat(g_trace, s, sizeof(g_trace) - strlen(g_trace) - 2);
  strncat(g_trace, " ", sizeof(g_trace) - strlen(g_trace) - 1);
}
static void clearTrace() { g_trace[0] = 0; }

struct TraceBehaviour : Behaviour {
  REFLECT(TraceBehaviour)
  SERIALIZE_FIELD(Scalar, speed, 2)
  void Awake() override { trace("Awake"); }
  void OnEnable() override { trace("OnEnable"); }
  void Start() override { trace("Start"); }
  void FixedUpdate() override { trace("Fixed"); }
  void Update() override { trace("Update"); }
  void LateUpdate() override { trace("Late"); }
  void OnDisable() override { trace("OnDisable"); }
  void OnDestroy() override { trace("OnDestroy"); }
};

struct Fixture {
  Arena arena;
  World world;
  Fixture() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
  }
};

TEST(world_lifecycle_order) {
  Fixture f;
  clearTrace();
  GameObject* go = f.world.createObject("probe");
  go->AddComponent<TraceBehaviour>();
  GameLoop loop;
  loop.init(f.world);
  VirtualGamepad pad;
  CHECK(loop.frame(1.0f / 60.0f, pad));
  CHECK(loop.frame(1.0f / 60.0f, pad));
  CHECK(strcmp(g_trace, "Awake OnEnable Start Fixed Update Late Fixed Update Late ") == 0);
  clearTrace();
  f.world.destroy(go);
  loop.frame(1.0f / 60.0f, pad);
  CHECK(strcmp(g_trace, "OnDisable OnDestroy ") == 0);
}

TEST(world_set_active_toggles_enable) {
  Fixture f;
  GameObject* go = f.world.createObject("probe");
  go->AddComponent<TraceBehaviour>();
  GameLoop loop;
  loop.init(f.world);
  VirtualGamepad pad;
  loop.frame(1.0f / 60.0f, pad);
  clearTrace();
  go->SetActive(false);
  go->SetActive(true);
  CHECK(strcmp(g_trace, "OnDisable OnEnable ") == 0);
  clearTrace();
  go->SetActive(false);
  loop.frame(1.0f / 60.0f, pad);
  CHECK(strstr(g_trace, "Update") == nullptr);  // inactive objects do not tick
}

TEST(ref_reads_null_after_destroy) {
  Fixture f;
  GameObject* go = f.world.createObject("a");
  MeshRenderer* mr = go->AddComponent<MeshRenderer>();
  Ref<GameObject> rgo = go->ref();
  Ref<MeshRenderer> rmr = f.world.ref(mr);
  CHECK(rgo.get() == go);
  CHECK(rmr.get() == mr);
  CHECK(static_cast<bool>(rgo));
  f.world.destroy(go);
  CHECK(rgo.get() == nullptr);  // null as soon as Destroy is called
  CHECK(rmr.get() == nullptr);
  f.world.flushDestroyed();
  CHECK(rgo.get() == nullptr);
  // Slot reuse must not resurrect the old Ref.
  GameObject* go2 = f.world.createObject("b");
  MeshRenderer* mr2 = go2->AddComponent<MeshRenderer>();
  CHECK(go2 == go);  // same storage slot
  CHECK(mr2 == mr);
  CHECK(rgo.get() == nullptr);
  CHECK(rmr.get() == nullptr);
  CHECK(f.world.ref(mr2).get() == mr2);
  CHECK(Ref<GameObject>().get() == nullptr);
}

TEST(world_hierarchy_and_children_destroy) {
  Fixture f;
  GameObject* parent = f.world.createObject("parent");
  GameObject* child = f.world.createObject("child", parent);
  parent->transform.lpos_ = Vector3(10, 0, 0);
  child->transform.lpos_ = Vector3(0, 1, 0);
  Vector3 wp = child->transform.position();
  CHECK_NEAR(wp.x, 10.0f, 1e-5f);
  CHECK_NEAR(wp.y, 1.0f, 1e-5f);
  child->transform.setPosition(Vector3(0, 0, 0));
  CHECK_NEAR(child->transform.localPosition().x, -10.0f, 1e-4f);
  Ref<GameObject> rc = child->ref();
  f.world.destroy(parent);
  CHECK(rc.get() == nullptr);
  f.world.flushDestroyed();
  CHECK_EQ(f.world.objectCount(), 0u);
}

TEST(world_capacity_is_fixed) {
  Fixture f;
  for (int i = 0; i < 16; ++i) CHECK(f.world.createObject("o") != nullptr);
  CHECK(f.world.createObject("overflow") == nullptr);
  GameObject* go = f.world.objectAt(0);
  for (int i = 0; i < 8; ++i) {
    GameObject* o = f.world.objectAt(static_cast<u32>(i));
    CHECK(o->AddComponent<BoxCollider>() != nullptr);
  }
  CHECK(go->AddComponent<SphereCollider>() != nullptr);
  CHECK(f.world.objectAt(8)->AddComponent<BoxCollider>() == nullptr);  // pool of 8 is full
}

TEST(world_no_heap_in_frames) {
  Fixture f;
  GameObject* go = f.world.createObject("probe");
  go->AddComponent<TraceBehaviour>();
  clearTrace();
  GameLoop loop;
  loop.init(f.world);
  VirtualGamepad pad;
  loop.frame(1.0f / 60.0f, pad);
  FrameHeapGuard::resetViolations();
  for (int i = 0; i < 30; ++i) CHECK(loop.frame(1.0f / 60.0f, pad));
  CHECK_EQ(FrameHeapGuard::violations(), 0u);
}

TEST(loop_fixed_timestep) {
  Fixture f;
  GameLoop loop;
  loop.init(f.world);
  VirtualGamepad pad;
  loop.frame(0.055f, pad);  // 3 steps of 1/60 fit into 0.055 s
  CHECK_EQ(loop.fixedStepCount(), 3u);
  CHECK(loop.interpolation() >= 0.0f && loop.interpolation() < 1.0f);
  loop.frame(10.0f, pad);  // clamped by maxFrameTime
  CHECK(loop.fixedStepCount() <= 3u + 8u);
  CHECK_EQ(loop.frameCount(), 2u);
}

TEST(input_actions_by_name) {
  Input::clearActions();
  Input::defineAxis2("Move", PackInputSource::Nub);
  Input::defineButton("Jump", PackInputSource::South);
  VirtualGamepad pad;
  pad.leftX = 0.5f;
  pad.leftY = -1.0f;
  Input::update(pad);
  CHECK_EQ(Input::GetAxis("Move"), 0.5f);
  CHECK_EQ(Input::GetAxis2("Move").y, -1.0f);
  CHECK(!Input::GetButton("Jump"));
  pad.held.south = true;
  Input::update(pad);
  CHECK(Input::GetButton("Jump"));
  CHECK(Input::GetButtonDown("Jump"));
  Input::update(pad);
  CHECK(Input::GetButton("Jump"));
  CHECK(!Input::GetButtonDown("Jump"));  // only on the first frame
}

TEST(null_backends_drive_loop_and_renderer) {
  Fixture f;
  GameObject* cam = f.world.createObject("cam");
  cam->AddComponent<Camera>();
  cam->transform.lpos_ = Vector3(0, 0, 5);
  GameObject* cube = f.world.createObject("cube");
  MeshRenderer* mr = cube->AddComponent<MeshRenderer>();
  mr->meshIndex = 0;
  NullPlatform platform;
  platform.maxPolls = 10;
  NullRenderer renderer;
  renderer.init(480, 272);
  GameLoop loop;
  loop.init(f.world);
  loop.run(platform, &renderer);
  CHECK(renderer.cameraSet);
  CHECK_EQ(renderer.frames, 10u);
  CHECK_EQ(renderer.totalDraws, 10u);
}

// Builds a small pack in memory and loads it through World::loadPack.
TEST(pack_load_roundtrip_and_version_refusal) {
  alignas(16) static u8 buf[4096];
  memset(buf, 0, sizeof(buf));
  const char strs[] = "root\0child\0Move\0";
  u32 off = 32 + 16 * 5;  // header + 5 sections
  off = static_cast<u32>(alignUp(off, 16));
  PackHeader* h = reinterpret_cast<PackHeader*>(buf);
  memcpy(h->magic, "RPAK", 4);
  h->version = kPackVersion;
  h->sectionCount = 5;
  h->alignment = 16;
  PackSection* s = reinterpret_cast<PackSection*>(buf + 32);
  auto place = [&](int i, u32 cc, u32 size, u32 count) -> u8* {
    s[i] = {cc, off, size, count};
    u8* p = buf + off;
    off = static_cast<u32>(alignUp(off + size, 16));
    return p;
  };
  memcpy(place(0, pack_section::STRS, sizeof(strs), 3), strs, sizeof(strs));
  PackObject* objs = reinterpret_cast<PackObject*>(place(1, pack_section::OBJS, sizeof(PackObject) * 2, 2));
  objs[0].nameStr = 0; objs[0].parentIndex = -1; objs[0].active = 1;
  Guid::parse("11111111-2222-4333-8444-555555555555", objs[0].guid);
  objs[1].nameStr = 5; objs[1].parentIndex = 0; objs[1].active = 1;
  PackTransform* x = reinterpret_cast<PackTransform*>(place(2, pack_section::XFRM, sizeof(PackTransform) * 2, 2));
  x[0].rotation[3] = 1; x[0].scale[0] = x[0].scale[1] = x[0].scale[2] = 1; x[0].position[0] = 3;
  x[1].rotation[3] = 1; x[1].scale[0] = x[1].scale[1] = x[1].scale[2] = 1;
  // One BoxCollider on object 1 with size (2,3,4), isTrigger = true.
  const TypeInfo& bc = *BoxCollider::staticTypeInfo();
  BoxCollider src;
  src.size = Vector3(2, 3, 4);
  src.isTrigger = true;
  PackComponent* comps = reinterpret_cast<PackComponent*>(place(3, pack_section::COMP, sizeof(PackComponent), 1));
  u8* fdat = place(4, pack_section::FDAT, blobSize(bc), 1);
  writeBlob(bc, &src, fdat);
  comps[0] = {bc.hash, 1, 0, blobSize(bc)};
  h->totalSize = off;

  Fixture f;
  PackView view;
  CHECK(view.open(buf, off));
  LoadedPack loaded;
  CHECK(f.world.loadPack(view, &loaded));
  CHECK_EQ(f.world.objectCount(), 2u);
  GameObject* child = f.world.findByName("child");
  CHECK(child != nullptr);
  CHECK(child->transform.parent() == &f.world.findByName("root")->transform);
  CHECK_NEAR(child->transform.position().x, 3.0f, 1e-5f);
  BoxCollider* got = child->GetComponent<BoxCollider>();
  CHECK(got != nullptr && got->isTrigger && got->size == Vector3(2, 3, 4));
  GameObject* root = f.world.findByGuid(objs[0].guid);
  CHECK(root != nullptr && strcmp(root->name, "root") == 0);

  h->version = 99;  // another version is refused with a log line
  PackView bad;
  struct Cap { bool sawName = false; } cap;
  CallbackSink sink([](const LogRecord& r, void* u) { if (strstr(r.message, "level1.rpak") && strstr(r.message, "version")) static_cast<Cap*>(u)->sawName = true; }, &cap);
  log::addSink(&sink);
  CHECK(!bad.open(buf, off, "level1.rpak"));
  log::removeSink(&sink);
  CHECK(cap.sawName);  // the error line names the file
}

TEST(loop_stops_when_application_quits) {
  Fixture f;
  struct QuitAfter : NullPlatform {
    u32 n = 0;
    void pollInput(VirtualGamepad& pad) override { pad = scripted; if (++n == 5) Application::Quit(); }
  } platform;  // never asks to quit itself
  GameLoop loop;
  loop.init(f.world);
  Application::reset();
  loop.run(platform, nullptr);
  CHECK_EQ(loop.frameCount(), 5u);
  CHECK(Application::quitRequested());
  Application::reset();
}
