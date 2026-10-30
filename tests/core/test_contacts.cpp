// F3.03 script API gaps: contact callbacks (OnTriggerEnter/Exit, the 2D names, OnCollisionEnter2D/Exit2D) and
// FindObjectOfType. The lifecycle order itself is covered by tests/game/test_lifecycle.cpp.
#include "rensga/Rensga.h"
#include "rensga/Physics.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

struct Counts {
  int enter = 0, exit = 0, enter2D = 0, exit2D = 0, collEnter = 0, collExit = 0;
  GameObject* lastOther = nullptr;
};
Counts g_a, g_b;

struct ContactProbeA : Behaviour {
  REFLECT(ContactProbeA)
  void OnTriggerEnter(GameObject* o) override { ++g_a.enter; g_a.lastOther = o; }
  void OnTriggerExit(GameObject*) override { ++g_a.exit; }
  void OnTriggerEnter2D(GameObject*) override { ++g_a.enter2D; }
  void OnTriggerExit2D(GameObject*) override { ++g_a.exit2D; }
  void OnCollisionEnter2D(GameObject* o) override { ++g_a.collEnter; g_a.lastOther = o; }
  void OnCollisionExit2D(GameObject*) override { ++g_a.collExit; }
};
struct ContactProbeB : Behaviour {
  REFLECT(ContactProbeB)
  void OnTriggerEnter(GameObject*) override { ++g_b.enter; }
  void OnTriggerExit(GameObject*) override { ++g_b.exit; }
  void OnCollisionEnter2D(GameObject*) override { ++g_b.collEnter; }
  void OnCollisionExit2D(GameObject*) override { ++g_b.collExit; }
};
struct FindProbe : Behaviour {
  REFLECT(FindProbe)
  SERIALIZE_FIELD(i32, tagValue, 0)
};

struct Fixture {
  Arena arena;
  World world;
  Fixture() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    world.init(c, arena);
    g_a = Counts();
    g_b = Counts();
    Gameplay2D::resetTriggerMemory();
    physics::resetTriggerMemory();
  }
  ~Fixture() { world.shutdown(); }
  GameObject* box2D(const char* name, Scalar x, Scalar y, bool trigger) {
    GameObject* go = world.createObject(name);
    go->transform.setPosition(Vector3(x, y, 0));
    BoxCollider2D* b = go->AddComponent<BoxCollider2D>();
    b->sizeX = 16;
    b->sizeY = 16;
    b->isTrigger = trigger;
    return go;
  }
};

}  // namespace

TEST(contacts_2d_trigger_enter_and_exit) {
  Fixture f;
  GameObject* trig = f.box2D("trigger", 100, 100, true);
  trig->AddComponent<ContactProbeA>();
  GameObject* mover = f.box2D("mover", 0, 0, false);
  mover->AddComponent<ContactProbeB>();
  f.world.startPending();
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.enter, 0);
  mover->transform.setPosition(Vector3(104, 104, 0));
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.enter, 1);
  CHECK_EQ(g_a.enter2D, 1);
  CHECK_EQ(g_b.enter, 1);
  CHECK(g_a.lastOther == mover);
  Gameplay2D::fixedStep(f.world);  // still inside: no new event
  CHECK_EQ(g_a.enter, 1);
  CHECK_EQ(g_a.exit, 0);
  mover->transform.setPosition(Vector3(0, 0, 0));
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.exit, 1);
  CHECK_EQ(g_a.exit2D, 1);
  CHECK_EQ(g_b.exit, 1);
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.exit, 1);
}

TEST(contacts_2d_solid_boxes_send_collision_events) {
  Fixture f;
  GameObject* a = f.box2D("a", 0, 0, false);
  a->AddComponent<ContactProbeA>();
  GameObject* b = f.box2D("b", 40, 0, false);
  b->AddComponent<ContactProbeB>();
  f.world.startPending();
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.collEnter, 0);
  b->transform.setPosition(Vector3(10, 0, 0));
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.collEnter, 1);
  CHECK_EQ(g_b.collEnter, 1);
  CHECK(g_a.lastOther == b);
  CHECK_EQ(g_a.enter, 0);  // not a trigger
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.collEnter, 1);
  b->transform.setPosition(Vector3(80, 0, 0));
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.collExit, 1);
  CHECK_EQ(g_b.collExit, 1);
}

TEST(contacts_2d_destroyed_object_sends_no_exit_to_the_dead) {
  Fixture f;
  GameObject* a = f.box2D("a", 0, 0, false);
  a->AddComponent<ContactProbeA>();
  GameObject* b = f.box2D("b", 4, 0, false);
  f.world.startPending();
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_a.collEnter, 1);
  f.world.destroy(b);
  f.world.flushDestroyed();
  Gameplay2D::fixedStep(f.world);  // the pair is gone; the survivor gets no callback with a dangling `other`
  CHECK_EQ(g_a.collExit, 0);
}

TEST(contacts_3d_trigger_exit) {
  Fixture f;
  GameObject* trig = f.world.createObject("zone");
  trig->transform.setPosition(Vector3(0, 0, 0));
  BoxCollider* bc = trig->AddComponent<BoxCollider>();
  bc->isTrigger = true;
  bc->size = Vector3(4, 4, 4);
  trig->AddComponent<ContactProbeA>();
  GameObject* pl = f.world.createObject("player");
  pl->transform.setPosition(Vector3(20, 0, 0));
  pl->AddComponent<CharacterController>()->gravity = 0;
  pl->AddComponent<ContactProbeB>();
  f.world.startPending();
  physics::fixedStep(f.world, 0.02f, nullptr);
  CHECK_EQ(g_a.enter, 0);
  pl->transform.setPosition(Vector3(0, 0, 0));
  physics::fixedStep(f.world, 0.02f, nullptr);
  CHECK_EQ(g_a.enter, 1);
  CHECK_EQ(g_b.enter, 1);
  pl->transform.setPosition(Vector3(20, 0, 0));
  physics::fixedStep(f.world, 0.02f, nullptr);
  CHECK_EQ(g_a.exit, 1);
  CHECK_EQ(g_b.exit, 1);
}

TEST(find_object_of_type) {
  Fixture f;
  CHECK(FindObjectOfType<FindProbe>() == nullptr);
  GameObject* a = f.world.createObject("a");
  GameObject* b = f.world.createObject("b");
  FindProbe* pa = a->AddComponent<FindProbe>();
  FindProbe* pb = b->AddComponent<FindProbe>();
  pa->tagValue = 1;
  pb->tagValue = 2;
  CHECK(FindObjectOfType<FindProbe>() == pa);
  FindProbe* all[4];
  CHECK_EQ(FindObjectsOfType<FindProbe>(all, 4), 2u);
  a->SetActive(false);
  CHECK(FindObjectOfType<FindProbe>() == pb);
  CHECK(FindObjectOfType<FindProbe>(true) == pa);
  CHECK_EQ(FindObjectsOfType<FindProbe>(all, 4), 1u);
}
