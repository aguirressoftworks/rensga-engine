// F6.08 (A2): console-light 3D physics. Shape pair tests (every pair, axis-aligned and rotated), Rigidbody dynamics (gravity, rest,
// AddForce modes, MovePosition, constraints, friction on ramps, sleeping), Enter / Stay / Exit callbacks, the layer matrix, every query,
// the CharacterController additions, and run-to-run determinism.
#include "../../engine/core/src/physics3d_internal.h"
#include "rensga/Physics.h"
#include "rensga/Rensga.h"
#include "rensga/core/physics3d.h"
#include "rensga/core/time.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::physics3d;

namespace {

constexpr Scalar kDt = 1.0f / 60.0f;

struct Events {
  int enter = 0, stay = 0, exit = 0, tEnter = 0, tStay = 0, tExit = 0;
  GameObject* other = nullptr;
  Vector3 normal, relVel;
  u32 contacts = 0;
  Scalar impulseLen = 0;
};
Events g_ev[2];

struct ProbeA : Behaviour {
  REFLECT(ProbeA)
  void OnCollisionEnter(const Collision& c) override {
    ++g_ev[0].enter;
    g_ev[0].other = c.gameObject;
    g_ev[0].contacts = c.contactCount;
    if (c.contactCount) g_ev[0].normal = c.contacts[0].normal;
    g_ev[0].relVel = c.relativeVelocity;
    g_ev[0].impulseLen = c.impulse.magnitude();
  }
  void OnCollisionStay(const Collision&) override { ++g_ev[0].stay; }
  void OnCollisionExit(const Collision&) override { ++g_ev[0].exit; }
  void OnTriggerEnter(GameObject* o) override { ++g_ev[0].tEnter; g_ev[0].other = o; }
  void OnTriggerStay(GameObject*) override { ++g_ev[0].tStay; }
  void OnTriggerExit(GameObject*) override { ++g_ev[0].tExit; }
};
struct ProbeB : Behaviour {
  REFLECT(ProbeB)
  void OnCollisionEnter(const Collision& c) override {
    ++g_ev[1].enter;
    g_ev[1].other = c.gameObject;
    if (c.contactCount) g_ev[1].normal = c.contacts[0].normal;
  }
  void OnCollisionStay(const Collision&) override { ++g_ev[1].stay; }
  void OnCollisionExit(const Collision&) override { ++g_ev[1].exit; }
  void OnTriggerEnter(GameObject*) override { ++g_ev[1].tEnter; }
  void OnTriggerStay(GameObject*) override { ++g_ev[1].tStay; }
  void OnTriggerExit(GameObject*) override { ++g_ev[1].tExit; }
};

struct Fix {
  Arena arena;
  World world;
  Fix() {
    arena.create(4 << 20);
    WorldConfig c;
    c.maxObjects = 96;
    c.maxComponentsPerType = 96;
    world.init(c, arena);
    resetSettings();
    resetState();
    g_ev[0] = Events();
    g_ev[1] = Events();
    TimeInternal::reset(kDt);
    TimeInternal::beginFixedStep(kDt);
  }
  ~Fix() { world.shutdown(); }
  GameObject* make(const char* name, const Vector3& p, const Quaternion& q = Quaternion::identity()) {
    GameObject* go = world.createObject(name);
    go->transform.setPosition(p);
    go->transform.setRotation(q);
    return go;
  }
  GameObject* box(const char* name, const Vector3& p, const Vector3& size, bool body = false, bool trigger = false,
                  const Quaternion& q = Quaternion::identity()) {
    GameObject* go = make(name, p, q);
    BoxCollider* b = go->AddComponent<BoxCollider>();
    b->size = size;
    b->isTrigger = trigger;
    if (body) go->AddComponent<Rigidbody>();
    return go;
  }
  GameObject* sphere(const char* name, const Vector3& p, Scalar r, bool body = false, bool trigger = false) {
    GameObject* go = make(name, p);
    SphereCollider* s = go->AddComponent<SphereCollider>();
    s->radius = r;
    s->isTrigger = trigger;
    if (body) go->AddComponent<Rigidbody>();
    return go;
  }
  GameObject* capsule(const char* name, const Vector3& p, Scalar r, Scalar h, bool body = false) {
    GameObject* go = make(name, p);
    CapsuleCollider* c = go->AddComponent<CapsuleCollider>();
    c->radius = r;
    c->height = h;
    if (body) go->AddComponent<Rigidbody>();
    return go;
  }
  void run(int n) {
    for (int i = 0; i < n; ++i) physics3d::step(world, kDt);
  }
};

Col mkBox(const Vector3& c, const Vector3& half, const Quaternion& q = Quaternion::identity()) {
  Col k;
  k.type = Shape::Box;
  k.c = c;
  k.h = half;
  k.ax[0] = q * Vector3(1, 0, 0);
  k.ax[1] = q * Vector3(0, 1, 0);
  k.ax[2] = q * Vector3(0, 0, 1);
  computeBounds(k);
  return k;
}
Col mkSphere(const Vector3& c, Scalar r) {
  Col k;
  k.type = Shape::Sphere;
  k.c = c;
  k.r = r;
  computeBounds(k);
  return k;
}
Col mkCapsule(const Vector3& c, Scalar r, Scalar hl, const Vector3& dir) {
  Col k;
  k.type = Shape::Capsule;
  k.c = c;
  k.r = r;
  k.hl = hl;
  k.dir = dir;
  computeBounds(k);
  return k;
}

}  // namespace

// ---- shape pairs ----------------------------------------------------------------------------------------------------------

TEST(phys3d_pair_sphere_sphere) {
  Manifold m;
  CHECK(collide(mkSphere(Vector3(0, 0, 0), 1), mkSphere(Vector3(1.5f, 0, 0), 1), kMargin, &m));
  CHECK_NEAR(m.sep, -0.5f, 1e-5f);
  CHECK_NEAR(m.n.x, 1.0f, 1e-5f);
  CHECK(!collide(mkSphere(Vector3(0, 0, 0), 1), mkSphere(Vector3(2.1f, 0, 0), 1), kMargin, &m));
  CHECK(collide(mkSphere(Vector3(0, 0, 0), 1), mkSphere(Vector3(2.01f, 0, 0), 1), kMargin, &m));  // within the speculative margin
  CHECK(m.sep > 0);
}

TEST(phys3d_pair_sphere_box_and_oriented) {
  Manifold m;
  Col box = mkBox(Vector3(0, 0, 0), Vector3(1, 1, 1));
  CHECK(collide(mkSphere(Vector3(0, 1.4f, 0), 0.5f), box, kMargin, &m));  // sphere above the box top: n runs sphere -> box (down)
  CHECK_NEAR(m.sep, -0.1f, 1e-5f);
  CHECK_NEAR(m.n.y, -1.0f, 1e-5f);
  // Sphere centre inside the box: pushed out through the nearest face.
  CHECK(collide(mkSphere(Vector3(0.9f, 0, 0), 0.1f), box, kMargin, &m));
  CHECK(m.sep < 0 && m.n.x < -0.99f);
  // Box rotated 45 degrees about Z: a diamond with its corner pointing up at y = sqrt(2).
  const Quaternion q45 = Quaternion::AngleAxis(45.0f, Vector3(0, 0, 1));
  Col diamond = mkBox(Vector3(0, 0, 0), Vector3(1, 1, 1), q45);
  CHECK(collide(mkSphere(Vector3(0, 1.8f, 0), 0.5f), diamond, kMargin, &m));
  CHECK_NEAR(m.sep, 1.8f - 1.41421356f - 0.5f, 2e-4f);
  CHECK(!collide(mkSphere(Vector3(1.2f, 1.2f, 0), 0.1f), diamond, 0.0f, &m) || m.sep >= 0);  // beyond the slanted face
}

TEST(phys3d_pair_capsule_sphere_capsule_box) {
  Manifold m;
  Col cap = mkCapsule(Vector3(0, 0, 0), 0.5f, 1.0f, Vector3(0, 1, 0));  // Y capsule from y=-1.5 to 1.5
  CHECK(collide(mkSphere(Vector3(0.8f, 0.3f, 0), 0.5f), cap, kMargin, &m));
  CHECK_NEAR(m.sep, -0.2f, 1e-5f);
  CHECK_NEAR(m.n.x, -1.0f, 1e-5f);  // sphere -> capsule is -x
  Col cap2 = mkCapsule(Vector3(0.9f, 0, 0), 0.5f, 1.0f, Vector3(0, 1, 0));
  CHECK(collide(cap, cap2, kMargin, &m));  // parallel capsules
  CHECK_NEAR(m.sep, -0.1f, 1e-5f);
  CHECK_NEAR(m.n.x, 1.0f, 1e-5f);
  Col crossCap = mkCapsule(Vector3(0, 0, 0.95f), 0.5f, 1.0f, Vector3(1, 0, 0));  // perpendicular, crossing in front
  CHECK(collide(cap, crossCap, kMargin, &m));
  CHECK_NEAR(m.sep, -0.05f, 1e-4f);
  Col box = mkBox(Vector3(0, -2.5f, 0), Vector3(2, 1, 2));  // top face at y = -1.5
  Manifold mb;
  CHECK(collide(cap, box, kMargin, &mb));  // capsule bottom at -1.5: touching
  CHECK(mb.sep > -0.001f && mb.sep < 0.001f);
  CHECK_NEAR(mb.n.y, -1.0f, 1e-4f);
  Col lying = mkCapsule(Vector3(0, -0.9f, 0), 0.5f, 1.0f, Vector3(1, 0, 0));  // lying capsule, bottom at y=-1.4: 0.1 above the top
  CHECK(!collide(lying, box, 0.0f, &mb) || mb.sep >= 0.0f - 1e-3f);
  Col lying2 = mkCapsule(Vector3(0, -1.1f, 0), 0.5f, 1.0f, Vector3(1, 0, 0));  // bottom at y=-1.6: 0.1 deep
  CHECK(collide(lying2, box, kMargin, &mb));
  CHECK_NEAR(mb.sep, -0.1f, 1e-3f);
  CHECK_NEAR(mb.n.y, -1.0f, 1e-3f);
}

TEST(phys3d_pair_box_box_sat) {
  Manifold m;
  Col a = mkBox(Vector3(0, 0, 0), Vector3(1, 1, 1));
  Col b = mkBox(Vector3(1.8f, 0.2f, 0), Vector3(1, 1, 1));
  CHECK(collide(a, b, kMargin, &m));
  CHECK_NEAR(m.sep, -0.2f, 1e-5f);
  CHECK_NEAR(m.n.x, 1.0f, 1e-5f);
  CHECK(!collide(a, mkBox(Vector3(2.5f, 0, 0), Vector3(1, 1, 1)), kMargin, &m));
  // Rotated 45 degrees about Y: a corner of b pokes into a's side.
  const Quaternion q = Quaternion::AngleAxis(45.0f, Vector3(0, 1, 0));
  Col c = mkBox(Vector3(2.3f, 0, 0), Vector3(1, 1, 1), q);  // corner reach 1.4142 -> overlaps x in [0.8858, 1]
  CHECK(collide(a, c, kMargin, &m));
  CHECK_NEAR(m.sep, -(1.0f + 1.41421356f - 2.3f), 5e-4f);
  CHECK_NEAR(m.n.x, 1.0f, 1e-3f);
  // A ramp: box rotated 30 degrees about Z under a unit box resting on it. n runs from the resting box (a) down into the ramp (b).
  const Quaternion r30 = Quaternion::AngleAxis(30.0f, Vector3(0, 0, 1));
  Col ramp = mkBox(Vector3(0, -2.0f, 0), Vector3(4, 0.5f, 2), r30);
  Col top = mkBox(Vector3(0, -0.85f, 0), Vector3(0.5f, 0.5f, 0.5f));
  const bool hit = collide(top, ramp, kMargin, &m);
  CHECK(hit);
  CHECK(m.n.y < -0.8f);  // the contact normal is the ramp's face normal (30 degrees from vertical)
}

// ---- Rigidbody dynamics ---------------------------------------------------------------------------------------------------

TEST(phys3d_box_falls_and_rests_with_events) {
  Fix f;
  GameObject* ground = f.box("ground", Vector3(0, -0.5f, 0), Vector3(20, 1, 20));
  ground->AddComponent<ProbeB>();
  GameObject* crate = f.box("crate", Vector3(0, 3, 0), Vector3(1, 1, 1), true);
  crate->AddComponent<ProbeA>();
  f.world.startPending();
  f.run(180);
  const Scalar y = crate->transform.position().y;
  CHECK(y > 0.48f && y < 0.52f);  // resting on the ground top (y = 0) with the half height 0.5 (+ a tolerated slop)
  Rigidbody* rb = crate->GetComponent<Rigidbody>();
  CHECK(rb->velocity().magnitude() < 0.05f);
  CHECK_EQ(g_ev[0].enter, 1);
  CHECK(g_ev[0].stay > 10);
  CHECK_EQ(g_ev[0].exit, 0);
  CHECK(g_ev[0].other == ground);
  CHECK(g_ev[0].normal.y > 0.99f);  // normal runs from the other collider toward this one: up
  CHECK(g_ev[0].relVel.y < -1.0f);  // it hit at speed
  CHECK(g_ev[0].impulseLen > 0);
  CHECK_EQ(g_ev[1].enter, 1);  // the ground got the callback as well
  CHECK(g_ev[1].normal.y < -0.99f);
  CHECK(g_ev[1].other == crate);
  // Rest -> sleeping after a while.
  f.run(60);
  CHECK(rb->IsSleeping());
  const Scalar rest = crate->transform.position().y;
  f.run(60);
  CHECK(crate->transform.position().y == rest);
  // Take the ground away: the sleeper wakes, falls, and the Exit callback runs.
  ground->SetActive(false);
  f.run(5);
  CHECK(!rb->IsSleeping());
  CHECK(crate->transform.position().y < rest);
  CHECK_EQ(g_ev[0].exit, 1);
}

TEST(phys3d_sphere_and_capsule_rest_on_box) {
  Fix f;
  f.box("ground", Vector3(0, -0.5f, 0), Vector3(20, 1, 20));
  GameObject* ball = f.sphere("ball", Vector3(-3, 2, 0), 0.5f, true);
  GameObject* pill = f.capsule("pill", Vector3(3, 2, 0), 0.4f, 1.8f, true);
  f.world.startPending();
  f.run(240);
  CHECK(ball->transform.position().y > 0.48f && ball->transform.position().y < 0.53f);
  CHECK(pill->transform.position().y > 0.88f && pill->transform.position().y < 0.93f);
}

TEST(phys3d_add_force_modes) {
  Fix f;
  GameObject* a = f.make("a", Vector3(0, 0, 0));
  Rigidbody* rb = a->AddComponent<Rigidbody>();
  rb->useGravity = false;
  rb->mass = 2.0f;
  f.world.startPending();
  rb->AddForce(Vector3(4, 0, 0), ForceMode::Impulse);  // dv = J / m
  f.run(1);
  CHECK_NEAR(rb->velocity().x, 2.0f, 1e-5f);
  rb->AddForce(Vector3(0, 3, 0), ForceMode::VelocityChange);
  f.run(1);
  CHECK_NEAR(rb->velocity().y, 3.0f, 1e-5f);
  rb->setVelocity(Vector3());
  rb->AddForce(Vector3(0, 0, 6), ForceMode::Force);  // a = F / m = 3; one step: dv = 3 dt
  f.run(1);
  CHECK_NEAR(rb->velocity().z, 3.0f * kDt, 1e-5f);
  rb->setVelocity(Vector3());
  rb->AddForce(Vector3(0, 0, 6), ForceMode::Acceleration);  // mass ignored
  f.run(1);
  CHECK_NEAR(rb->velocity().z, 6.0f * kDt, 1e-5f);
  // Drag slows a body.
  rb->drag = 5.0f;
  rb->setVelocity(Vector3(10, 0, 0));
  f.run(60);
  CHECK(rb->velocity().x < 1.0f);
}

TEST(phys3d_gravity_setting_and_constraints) {
  Fix f;
  GameObject* a = f.make("a", Vector3(0, 10, 0));
  Rigidbody* rb = a->AddComponent<Rigidbody>();
  GameObject* b = f.make("b", Vector3(5, 10, 0));
  Rigidbody* rbb = b->AddComponent<Rigidbody>();
  rbb->constraints = RigidbodyConstraints::FreezePositionY;
  rbb->setVelocity(Vector3(1, 0, 0));
  Physics::setGravity(Vector3(0, -20.0f, 0));
  CHECK_NEAR(Physics::gravity().y, -20.0f, 1e-6f);
  f.world.startPending();
  f.run(60);
  CHECK_NEAR(rb->velocity().y, -20.0f, 0.05f);
  CHECK(a->transform.position().y < 10 - 5.0f);
  CHECK_NEAR(b->transform.position().y, 10.0f, 1e-6f);
  CHECK(b->transform.position().x > 5.9f);
}

TEST(phys3d_kinematic_move_position_pushes_bodies) {
  Fix f;
  GameObject* wall = f.box("wall", Vector3(-4, 0, 0), Vector3(1, 2, 2));
  Rigidbody* kr = wall->AddComponent<Rigidbody>();
  kr->isKinematic = true;
  kr->useGravity = false;
  GameObject* crate = f.box("crate", Vector3(-1, 0, 0), Vector3(1, 1, 1), true);
  crate->GetComponent<Rigidbody>()->useGravity = false;
  f.world.startPending();
  for (int i = 0; i < 90; ++i) {
    kr->MovePosition(wall->transform.position() + Vector3(0.05f, 0, 0));
    f.run(1);
  }
  // The wall moved 4.5 units to x = 0.5; its right face (x = 1.0) must have pushed the crate ahead of it.
  CHECK_NEAR(wall->transform.position().x, 0.5f, 1e-4f);
  CHECK(crate->transform.position().x > 1.45f);  // crate left face >= wall right face (x = 1.0)
  CHECK(crate->transform.position().x - 0.5f >= wall->transform.position().x + 0.5f - 0.03f);
}

TEST(phys3d_ramp_friction_hold_and_slide) {
  Fix f;
  const Quaternion gentle = Quaternion::AngleAxis(-15.0f, Vector3(0, 0, 1));  // rises toward +x
  GameObject* ramp = f.box("ramp", Vector3(0, 0, 0), Vector3(12, 1, 6), false, false, gentle);
  (void)ramp;
  GameObject* crate = f.box("crate", Vector3(0, 1.5f, 0), Vector3(1, 1, 1), true);
  f.world.startPending();
  f.run(40);  // lands
  const Vector3 p0 = crate->transform.position();
  f.run(120);
  const Vector3 p1 = crate->transform.position();
  CHECK((p1 - p0).magnitude() < 0.1f);  // tan 15 = 0.27 < friction 0.4: it holds
  Fix g;
  const Quaternion steep = Quaternion::AngleAxis(-40.0f, Vector3(0, 0, 1));
  g.box("ramp", Vector3(0, 0, 0), Vector3(12, 1, 6), false, false, steep);
  GameObject* c2 = g.box("crate", Vector3(0, 1.5f, 0), Vector3(1, 1, 1), true);
  g.world.startPending();
  g.run(40);
  const Vector3 q0 = c2->transform.position();
  g.run(60);
  CHECK(c2->transform.position().x > q0.x + 0.5f);  // the ramp rises toward +x here, so the crate slides downhill toward +x
}

TEST(phys3d_layer_matrix) {
  Fix f;
  GameObject* ground = f.box("ground", Vector3(0, -0.5f, 0), Vector3(20, 1, 20));
  ground->layer = 3;
  GameObject* crate = f.box("crate", Vector3(0, 1.5f, 0), Vector3(1, 1, 1), true);
  crate->layer = 4;
  f.world.startPending();
  CHECK(!Physics::GetIgnoreLayerCollision(3, 4));
  Physics::IgnoreLayerCollision(3, 4, true);
  CHECK(Physics::GetIgnoreLayerCollision(4, 3));
  f.run(120);
  CHECK(crate->transform.position().y < -1.0f);  // fell through the ignored layer
  Physics::IgnoreLayerCollision(3, 4, false);
  CHECK(!Physics::GetIgnoreLayerCollision(4, 3));
}

TEST(phys3d_trigger_enter_stay_exit) {
  Fix f;
  GameObject* zone = f.box("zone", Vector3(0, 0, 0), Vector3(4, 4, 4), false, true);
  zone->AddComponent<ProbeB>();
  GameObject* ball = f.sphere("ball", Vector3(-6, 0, 0), 0.5f, true);
  ball->AddComponent<ProbeA>();
  Rigidbody* rb = ball->GetComponent<Rigidbody>();
  rb->useGravity = false;
  rb->setVelocity(Vector3(3, 0, 0));
  f.world.startPending();
  f.run(40);
  CHECK_EQ(g_ev[0].tEnter, 0);
  f.run(60);  // x from -4 to 1: inside
  CHECK_EQ(g_ev[0].tEnter, 1);
  CHECK_EQ(g_ev[1].tEnter, 1);
  CHECK(g_ev[0].tStay > 3 && g_ev[1].tStay > 3);
  f.run(120);
  CHECK_EQ(g_ev[0].tExit, 1);
  CHECK_EQ(g_ev[1].tExit, 1);
  // A trigger never blocks.
  CHECK(ball->transform.position().x > 5.0f);
}

TEST(phys3d_no_bodies_costs_nothing_and_changes_nothing) {
  Fix f;
  GameObject* b = f.box("static", Vector3(1, 2, 3), Vector3(1, 1, 1));
  f.world.startPending();
  f.run(10);
  CHECK(b->transform.position() == Vector3(1, 2, 3));
  CHECK_EQ(stats().bodies, 0u);
}

// ---- queries --------------------------------------------------------------------------------------------------------------

TEST(phys3d_raycast_shapes_layers_triggers) {
  Fix f;
  GameObject* box = f.box("box", Vector3(0, 0, 10), Vector3(2, 2, 2));  // front face z = 9
  GameObject* sph = f.sphere("sph", Vector3(5, 0, 10), 1.0f);
  GameObject* cap = f.capsule("cap", Vector3(-5, 0, 10), 0.5f, 3.0f);
  GameObject* trig = f.box("trig", Vector3(0, 0, 5), Vector3(2, 2, 0.2f), false, true);
  f.world.startPending();
  RaycastHit h;
  CHECK(Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), &h, 100.0f, LayerMask::All(), QueryTriggerInteraction::Ignore));
  CHECK(h.gameObject == box);
  CHECK_NEAR(h.distance, 9.0f, 1e-4f);
  CHECK_NEAR(h.point.z, 9.0f, 1e-4f);
  CHECK_NEAR(h.normal.z, -1.0f, 1e-5f);
  CHECK(Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), &h, 100.0f, LayerMask::All(), QueryTriggerInteraction::Collide));
  CHECK(h.gameObject == trig && h.isTrigger);
  CHECK(Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), &h));  // UseGlobal: queries hit triggers by default
  CHECK(h.gameObject == trig);
  CHECK(Physics::Raycast(Vector3(5, 0, 0), Vector3(0, 0, 1), &h));
  CHECK(h.gameObject == sph);
  CHECK_NEAR(h.distance, 9.0f, 1e-4f);
  CHECK_NEAR(h.normal.z, -1.0f, 1e-4f);
  CHECK(Physics::Raycast(Vector3(-5, 0, 0), Vector3(0, 0, 1), &h));
  CHECK(h.gameObject == cap);
  CHECK_NEAR(h.distance, 9.5f, 1e-4f);
  CHECK(Physics::Raycast(Vector3(-5, 1.2f, 0), Vector3(0, 0, 1), &h));  // above the cylinder part, through the top cap sphere
  CHECK(h.gameObject == cap && h.distance > 9.5f);
  CHECK(!Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), 3.0f));  // too short
  CHECK(!Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 1, 0), 100.0f));
  // Layer masks.
  box->layer = 7;
  trig->layer = 8;
  CHECK(Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), &h, 100.0f, LayerMask::Layer(7)));
  CHECK(h.gameObject == box);
  CHECK(!Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), &h, 100.0f, LayerMask::Layer(5)));
  CHECK(Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), &h, 100.0f, LayerMask(~(1u << 8))));  // everything but the trigger's layer
  CHECK(h.gameObject == box);
  // Linecast and the all-hits query (nearest first).
  CHECK(Physics::Linecast(Vector3(0, 0, 0), Vector3(0, 0, 20), &h, LayerMask::All(), QueryTriggerInteraction::Collide));
  CHECK(h.gameObject == trig);
  CHECK(!Physics::Linecast(Vector3(0, 0, 0), Vector3(0, 0, 4)));
  RaycastHit all[4];
  const u32 n = Physics::RaycastNonAlloc(Ray(Vector3(0, 0, 0), Vector3(0, 0, 1)), all, 4);
  CHECK_EQ(n, 2u);
  CHECK(all[0].gameObject == trig && all[1].gameObject == box);
  CHECK(all[0].distance < all[1].distance);
  // A ray that starts inside a collider does not hit it.
  CHECK(!Physics::Raycast(Vector3(0, 0, 10), Vector3(0, 0, 1), &h, 100.0f, LayerMask::Layer(7)));
}

TEST(phys3d_raycast_oriented_box) {
  Fix f;
  const Quaternion q = Quaternion::AngleAxis(45.0f, Vector3(0, 1, 0));
  GameObject* diamond = f.box("diamond", Vector3(0, 0, 10), Vector3(2, 2, 2), false, false, q);
  f.world.startPending();
  RaycastHit h;
  CHECK(Physics::Raycast(Vector3(0, 0, 0), Vector3(0, 0, 1), &h));
  CHECK(h.gameObject == diamond);
  CHECK_NEAR(h.distance, 10.0f - 1.41421356f, 1e-3f);  // the corner points at the ray
  CHECK(!Physics::Raycast(Vector3(1.5f, 0, 0), Vector3(0, 0, 1), &h));  // beyond the slanted faces
}

TEST(phys3d_spherecast_overlap_check) {
  Fix f;
  GameObject* wall = f.box("wall", Vector3(0, 0, 10), Vector3(10, 10, 1));  // front z = 9.5
  f.sphere("s1", Vector3(2, 0, 2), 0.5f);
  f.sphere("s2", Vector3(0, 0, 1), 0.5f);
  GameObject* trig = f.sphere("s3", Vector3(0, 1, 0), 0.5f, false, true);
  f.world.startPending();
  RaycastHit h;
  CHECK(Physics::SphereCast(Vector3(0, 0, 3), 0.5f, Vector3(0, 0, 1), &h, 100.0f, LayerMask::Layer(9)) == false);
  wall->layer = 9;
  CHECK(Physics::SphereCast(Vector3(0, 0, 3), 0.5f, Vector3(0, 0, 1), &h, 100.0f, LayerMask::Layer(9)));
  CHECK(h.gameObject == wall);
  CHECK_NEAR(h.distance, 9.5f - 0.5f - 3.0f, 2e-3f);
  CHECK_NEAR(h.normal.z, -1.0f, 1e-3f);
  CHECK_NEAR(h.point.z, 9.5f, 2e-3f);
  GameObject* out[8];
  u32 n = Physics::OverlapSphere(Vector3(0, 0, 0), 1.2f, out, 8, LayerMask::All(), QueryTriggerInteraction::Ignore);
  CHECK_EQ(n, 1u);  // s2 only; the trigger is ignored
  CHECK(out[0] != trig);
  n = Physics::OverlapSphere(Vector3(0, 0, 0), 1.2f, out, 8, LayerMask::All(), QueryTriggerInteraction::Collide);
  CHECK_EQ(n, 2u);
  n = Physics::OverlapSphere(Vector3(0, 0, 0), 3.0f, out, 8, LayerMask::All(), QueryTriggerInteraction::Collide);
  CHECK_EQ(n, 3u);
  CHECK(Physics::CheckSphere(Vector3(0, 0, 0), 1.2f));
  CHECK(!Physics::CheckSphere(Vector3(0, 5, 0), 0.5f));
  u32 two = Physics::OverlapSphere(Vector3(0, 0, 0), 3.0f, out, 2, LayerMask::All(), QueryTriggerInteraction::Collide);
  CHECK_EQ(two, 2u);
}

// ---- CharacterController ----------------------------------------------------------------------------------------------------

TEST(phys3d_character_controller_ramp_capsule_rigidbody) {
  Fix f;
  f.box("floor", Vector3(0, -0.5f, 0), Vector3(40, 1, 40));
  // A 25 degree ramp rising toward +x, whose low end sits on the floor, a capsule post and a crate on a Rigidbody.
  GameObject* ramp = f.box("ramp", Vector3(4, 0.6f, 0), Vector3(6, 0.4f, 4), false, false, Quaternion::AngleAxis(25.0f, Vector3(0, 0, 1)));
  (void)ramp;
  f.capsule("post", Vector3(-4, 1, 0), 0.5f, 2.0f);
  GameObject* crate = f.box("crate", Vector3(0, 0.5f, 4), Vector3(1, 1, 1), true);
  crate->GetComponent<Rigidbody>()->isKinematic = true;
  GameObject* hero = f.make("hero", Vector3(-1, 1.0f, 0));
  CharacterController* cc = hero->AddComponent<CharacterController>();
  CHECK_NEAR(cc->slopeLimit, 45.0f, 1e-6f);
  f.world.startPending();
  for (int i = 0; i < 30; ++i) { cc->Move(Vector3()); physics3d::step(f.world, kDt); }
  CHECK(cc->isGrounded());
  // Walk into the capsule post: blocked.
  for (int i = 0; i < 120; ++i) { cc->Move(Vector3(-0.05f, 0, 0)); physics3d::step(f.world, kDt); }
  CHECK(hero->transform.position().x > -4.0f + 0.5f + 0.5f - 0.05f);
  // Walk into the kinematic Rigidbody crate (its collider is a box on a Rigidbody: an obstacle for the controller).
  hero->transform.setPosition(Vector3(0, 1.0f, 0.5f));
  for (int i = 0; i < 120; ++i) { cc->Move(Vector3(0, 0, 0.05f)); physics3d::step(f.world, kDt); }
  CHECK(hero->transform.position().z < 4.0f - 0.5f - 0.5f + 0.05f);
  // Up the ramp: grounded and rising (slope 25 < 45).
  hero->transform.setPosition(Vector3(1.5f, 1.2f, 0));
  const Scalar y0 = hero->transform.position().y;
  bool grounded = false;
  for (int i = 0; i < 100; ++i) {
    cc->Move(Vector3(0.03f, 0, 0));
    physics3d::step(f.world, kDt);
    grounded = cc->isGrounded();
  }
  CHECK(grounded);
  CHECK(hero->transform.position().y > y0 + 0.4f);
  CHECK(cc->groundNormal.y > 0.85f);
  // A steep ramp is not ground when slopeLimit is small.
  cc->slopeLimit = 10.0f;
  for (int i = 0; i < 5; ++i) { cc->Move(Vector3(0.03f, 0, 0)); physics3d::step(f.world, kDt); }
  CHECK(!cc->isGrounded() || cc->groundNormal.y < Mathf::Cos(10.0f * Mathf::Deg2Rad));
}

TEST(phys3d_character_controller_pushes_dynamic_crate) {
  Fix f;
  f.box("floor", Vector3(0, -0.5f, 0), Vector3(40, 1, 40));
  GameObject* crate = f.box("crate", Vector3(2, 0.5f, 0), Vector3(1, 1, 1), true);
  GameObject* hero = f.make("hero", Vector3(0, 1.0f, 0));
  CharacterController* cc = hero->AddComponent<CharacterController>();
  f.world.startPending();
  for (int i = 0; i < 20; ++i) { cc->Move(Vector3()); physics3d::step(f.world, kDt); }
  for (int i = 0; i < 100; ++i) { cc->Move(Vector3(0.04f, 0, 0)); physics3d::step(f.world, kDt); }
  CHECK(crate->transform.position().x > 3.0f);  // pushed ahead of the controller (hero reaches x = 4)
  CHECK(hero->transform.position().x > 3.0f);
}

// ---- determinism ------------------------------------------------------------------------------------------------------------

namespace {
u32 hashScene() {
  Fix f;
  f.box("ground", Vector3(0, -0.5f, 0), Vector3(30, 1, 30));
  f.box("ramp", Vector3(0, 1.0f, 0), Vector3(8, 0.5f, 4), false, false, Quaternion::AngleAxis(-20.0f, Vector3(0, 0, 1)));
  for (int i = 0; i < 6; ++i) f.box("c", Vector3(-3.0f + 0.1f * static_cast<Scalar>(i), 4.0f + 1.2f * static_cast<Scalar>(i), 0.2f * static_cast<Scalar>(i % 2)), Vector3(1, 1, 1), true);
  for (int i = 0; i < 3; ++i) f.sphere("s", Vector3(2.0f + static_cast<Scalar>(i), 6.0f, 0), 0.5f, true);
  f.capsule("p", Vector3(5, 6, 1), 0.4f, 1.8f, true);
  f.world.startPending();
  f.run(400);
  u32 h = 2166136261u;
  for (u32 i = 0; i < f.world.objectHighWater(); ++i) {
    GameObject* go = f.world.objectAt(i);
    if (!go) continue;
    const Vector3 p = go->transform.position();
    const u8* b = reinterpret_cast<const u8*>(&p);
    for (size_t k = 0; k < sizeof p; ++k) { h ^= b[k]; h *= 16777619u; }
  }
  return h;
}
}  // namespace

TEST(phys3d_deterministic_and_stack_settles) {
  const u32 a = hashScene();
  const u32 b = hashScene();
  CHECK_EQ(a, b);
  CHECK(a != 0);
}

TEST(phys3d_stack_of_crates_stays_up) {
  Fix f;
  f.box("ground", Vector3(0, -0.5f, 0), Vector3(20, 1, 20));
  GameObject* c[4];
  for (int i = 0; i < 4; ++i) c[i] = f.box("c", Vector3(0, 0.5f + 1.0f * static_cast<Scalar>(i), 0), Vector3(1, 1, 1), true);
  f.world.startPending();
  f.run(300);
  for (int i = 0; i < 4; ++i) {
    const Vector3 p = c[i]->transform.position();
    CHECK(Mathf::Abs(p.x) < 0.05f && Mathf::Abs(p.z) < 0.05f);
    CHECK(p.y > 0.45f + static_cast<Scalar>(i) * 0.97f && p.y < 0.6f + static_cast<Scalar>(i) * 1.05f);
  }
}

// ---- 2D queries and callbacks -------------------------------------------------------------------------------------------------

namespace {

struct Events2D {
  int enter = 0, stay = 0, exit = 0, tEnter = 0, tStay = 0, tExit = 0, tStay3 = 0;
  Vector2 normal, point, relVel;
  GameObject* other = nullptr;
};
Events2D g_e2;
struct Probe2D : Behaviour {
  REFLECT(Probe2D)
  void OnCollisionEnter2D(const Collision2D& c) override { ++g_e2.enter; g_e2.normal = c.normal; g_e2.point = c.point; g_e2.other = c.gameObject; g_e2.relVel = c.relativeVelocity; }
  void OnCollisionStay2D(const Collision2D&) override { ++g_e2.stay; }
  void OnCollisionExit2D(const Collision2D&) override { ++g_e2.exit; }
  void OnTriggerEnter2D(GameObject*) override { ++g_e2.tEnter; }
  void OnTriggerStay2D(GameObject*) override { ++g_e2.tStay; }
  void OnTriggerStay(GameObject*) override { ++g_e2.tStay3; }
  void OnTriggerExit2D(GameObject*) override { ++g_e2.tExit; }
};

constexpr u32 kW = 20, kH = 10, kTs = 16;

struct Fix2D {
  Arena arena;
  World world;
  u16 tiles[kW * kH];
  u8 flags[4];
  alignas(16) u8 blob[kW * kH * 2 + 16];
  PackTilemapRec rec = {};
  PackAssets assets;
  Fix2D() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    world.init(c, arena);
    for (u16& t : tiles) t = 0xFFFF;
    memset(flags, 0, sizeof flags);
    flags[1] = PackTileFlag::kSolid;
    g_e2 = Events2D();
    Gameplay2D::resetTriggerMemory();
    Physics2D::resetMatrix();
    TimeInternal::reset(kDt);
    TimeInternal::beginFixedStep(kDt);
  }
  ~Fix2D() { GfxAssets::unbind(); world.shutdown(); }
  GameObject* box(const char* n, Scalar x, Scalar y, Scalar w, Scalar h, bool trigger) {
    GameObject* go = world.createObject(n);
    go->transform.setPosition(Vector3(x, y, 0));
    BoxCollider2D* b = go->AddComponent<BoxCollider2D>();
    b->sizeX = w;
    b->sizeY = h;
    b->isTrigger = trigger;
    return go;
  }
  GameObject* bindMap() {  // a floor of solid tiles in rows 8 and 9 (surface at y = 128) and a pillar at column 10, rows 4..7
    for (u32 x = 0; x < kW; ++x) { tiles[8 * kW + x] = 1; tiles[9 * kW + x] = 1; }
    for (u32 y = 4; y < 8; ++y) tiles[y * kW + 10] = 1;
    memcpy(blob, tiles, sizeof tiles);
    memcpy(blob + sizeof tiles, flags, sizeof flags);
    Guid::parse("00000000-0000-4000-8000-0000000000b2", rec.guid);
    rec.width = kW; rec.height = kH; rec.tileSize = kTs;
    rec.tilesOffset = 0; rec.solidOffset = sizeof tiles; rec.solidCount = 4;
    assets.blob = blob; assets.blobSize = sizeof blob; assets.tilemaps = &rec; assets.tilemapCount = 1;
    GfxAssets::bind(nullptr, &assets, nullptr, 0);
    GameObject* map = world.createObject("map");
    map->AddComponent<Tilemap>()->map = rec.guid;
    map->AddComponent<TilemapCollider2D>();
    return map;
  }
};

}  // namespace

TEST(phys2d_raycast_and_overlap_on_boxes) {
  Fix2D f;
  GameObject* a = f.box("a", 100, 0, 16, 16, false);
  GameObject* b = f.box("b", 200, 0, 16, 16, true);
  b->layer = 5;
  f.world.startPending();
  RaycastHit2D h = Physics2D::Raycast(Vector2(0, 8), Vector2(1, 0));
  CHECK(h.hit() && h.gameObject == a);
  CHECK_NEAR(h.distance, 100.0f, 1e-3f);
  CHECK_NEAR(h.point.x, 100.0f, 1e-3f);
  CHECK_NEAR(h.normal.x, -1.0f, 1e-6f);
  h = Physics2D::Raycast(Vector2(0, 8), Vector2(1, 0), 50.0f);
  CHECK(!h.hit());
  h = Physics2D::Raycast(Vector2(0, 8), Vector2(1, 0), Physics2D::kInfinity, LayerMask::Layer(5));
  CHECK(h.hit() && h.gameObject == b);
  h = Physics2D::Raycast(Vector2(108, -50), Vector2(0, 1));
  CHECK(h.hit() && h.gameObject == a && h.normal.y < -0.99f && h.distance > 49.9f && h.distance < 50.1f);
  CHECK(!Physics2D::Raycast(Vector2(0, 100), Vector2(1, 0)).hit());
  CHECK(Physics2D::OverlapCircle(Vector2(95, 8), 6.0f) == a);
  CHECK(Physics2D::OverlapCircle(Vector2(80, 8), 6.0f) == nullptr);
  CHECK(Physics2D::OverlapBox(Vector2(108, 8), Vector2(4, 4)) == a);
  GameObject* out[4];
  CHECK_EQ(Physics2D::OverlapBoxAll(Vector2(150, 8), Vector2(300, 4), out, 4), 2u);
  CHECK_EQ(Physics2D::OverlapCircleAll(Vector2(150, 8), 400.0f, out, 1), 1u);
  CHECK(Physics2D::OverlapBox(Vector2(108, 8), Vector2(4, 4), LayerMask::Layer(5)) == nullptr);
}

TEST(phys2d_raycast_and_overlap_on_tilemap) {
  Fix2D f;
  GameObject* map = f.bindMap();
  GameObject* coin = f.box("coin", 40, 100, 8, 8, true);
  f.world.startPending();
  RaycastHit2D h = Physics2D::Raycast(Vector2(20, 20), Vector2(0, 1));  // straight down onto the floor (y = 128)
  CHECK(h.hit() && h.gameObject == map);
  CHECK_NEAR(h.distance, 108.0f, 1e-2f);
  CHECK_NEAR(h.normal.y, -1.0f, 1e-6f);
  h = Physics2D::Raycast(Vector2(20, 90), Vector2(1, 0));  // sideways into the pillar's left face (x = 160)
  CHECK(h.hit() && h.gameObject == map && h.distance > 139.9f && h.distance < 140.1f && h.normal.x < -0.99f);
  h = Physics2D::Raycast(Vector2(20, 90), Vector2(1, 0), Physics2D::kInfinity, LayerMask::Layer(3));
  CHECK(!h.hit());
  CHECK(Physics2D::OverlapCircle(Vector2(100, 124), 8.0f) == map);   // reaches into the floor
  CHECK(Physics2D::OverlapCircle(Vector2(100, 100), 8.0f) == nullptr);
  CHECK(Physics2D::OverlapBox(Vector2(168, 90), Vector2(10, 10)) == map);  // the pillar
  (void)coin;
}

TEST(phys2d_stay_and_collision2d_callbacks) {
  Fix2D f;
  GameObject* a = f.box("a", 0, 0, 16, 16, false);
  a->AddComponent<Probe2D>();
  GameObject* b = f.box("b", 24, 0, 16, 16, false);
  GameObject* zone = f.box("zone", 200, 0, 16, 16, true);
  zone->AddComponent<Probe2D>();
  GameObject* walker = f.box("walker", 400, 0, 16, 16, false);
  f.world.startPending();
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_e2.enter, 0);
  b->transform.setPosition(Vector3(10, 0, 0));  // overlaps by 6 in x
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_e2.enter, 1);
  CHECK(g_e2.other == b);
  CHECK(g_e2.normal.x < -0.99f);  // from the other box toward `a`: b is on the right, so a is pushed left
  CHECK_NEAR(g_e2.point.x, 13.0f, 1e-3f);
  Gameplay2D::fixedStep(f.world);
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_e2.enter, 1);
  CHECK_EQ(g_e2.stay, 2);
  b->transform.setPosition(Vector3(100, 0, 0));
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_e2.exit, 1);
  // Trigger zone: Enter, then Stay (both the 2D and the 3D name), then Exit.
  walker->transform.setPosition(Vector3(204, 0, 0));
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_e2.tEnter, 1);
  Gameplay2D::fixedStep(f.world);
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_e2.tStay, 2);
  CHECK_EQ(g_e2.tStay3, 2);
  walker->transform.setPosition(Vector3(400, 0, 0));
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_e2.tExit, 1);
}
