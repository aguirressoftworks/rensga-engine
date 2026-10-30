// MVP-30: minimal physics. CharacterController::Move (gravity + axis-separated AABB resolution
// against static BoxColliders, step offset, grounded flag) and sphere/box trigger enter events.
// Colliders are axis-aligned: rotation is ignored, scale is the product of local scales up the
// hierarchy. A controller's box is centred on its transform with half extents (radius, height/2, radius).
#include "rensga/Physics.h"

#include "rensga/core/components.h"
#include "rensga/core/time.h"

namespace rensga {

namespace {

constexpr Scalar kEps = 1e-4f;
constexpr Scalar kMaxFallSpeed = 50.0f;

struct Aabb {
  Vector3 min, max;
};

Vector3 worldScale(const Transform* t) { return t->lossyScale(); }

Aabb boxWorld(const BoxCollider& b) {
  const Transform* t = b.transform();
  Vector3 s = worldScale(t);
  Vector3 c = t->position() + Vector3(b.center.x * s.x, b.center.y * s.y, b.center.z * s.z);
  Vector3 h(Mathf::Abs(b.size.x * s.x) * 0.5f, Mathf::Abs(b.size.y * s.y) * 0.5f, Mathf::Abs(b.size.z * s.z) * 0.5f);
  return {c - h, c + h};
}

bool overlaps(const Aabb& a, const Aabb& b) {
  return a.min.x < b.max.x - kEps && a.max.x > b.min.x + kEps && a.min.y < b.max.y - kEps &&
         a.max.y > b.min.y + kEps && a.min.z < b.max.z - kEps && a.max.z > b.min.z + kEps;
}

Aabb around(const Vector3& p, const Vector3& half) { return {p - half, p + half}; }

bool anyOverlap(World& world, const GameObject* self, const Aabb& a, const Aabb** hit, Aabb* hitStore) {
  bool found = false;
  world.forEach<BoxCollider>([&](BoxCollider& b) {
    if (found || b.isTrigger || !b.enabled()) return;
    GameObject* go = b.gameObject();
    if (!go || go == self || !go->activeInHierarchy()) return;
    Aabb w = boxWorld(b);
    if (overlaps(a, w)) {
      found = true;
      if (hitStore) *hitStore = w;
    }
  });
  if (hit) *hit = found ? hitStore : nullptr;
  return found;
}

// Moves `pos` along one axis (0=x,1=y,2=z) by `d`, stopping at the first static box.
// Returns true if blocked.
bool sweepAxis(World& world, const GameObject* self, Vector3& pos, const Vector3& half, int axis, Scalar d) {
  if (d == 0) return false;
  Scalar* p = &pos.x + axis;
  *p += d;
  Aabb hitBox;
  bool blocked = false;
  // Resolve against every box we now overlap (at most a few iterations).
  for (int i = 0; i < 4; ++i) {
    if (!anyOverlap(world, self, around(pos, half), nullptr, &hitBox)) break;
    blocked = true;
    const Scalar h = (&half.x)[axis];
    const Scalar lo = (&hitBox.min.x)[axis], hi = (&hitBox.max.x)[axis];
    *p = d > 0 ? lo - h : hi + h;
  }
  return blocked;
}

struct TriggerMemory {
  u32 generation = 0;
  u32 mask = 0;  // bit i: controller i overlapped last step
};
TriggerMemory g_memory[1024];

Scalar sqr(Scalar v) { return v * v; }

}  // namespace

void CharacterController::Move(const Vector3& motion) {
  GameObject* self = gameObject();
  World& world = *self->world;
  Transform* t = transform();
  const Scalar dt = Time::fixedDeltaTime();
  const Vector3 half(radius, height * 0.5f, radius);

  velocity.y += gravity * dt;
  if (velocity.y < -kMaxFallSpeed) velocity.y = -kMaxFallSpeed;
  const bool wasGrounded = grounded;
  grounded = false;

  Vector3 pos = t->position();
  Vector3 delta(motion.x, motion.y + velocity.y * dt, motion.z);

  // Horizontal axes, with step-up when grounded.
  for (int axis = 0; axis < 3; axis += 2) {
    Scalar d = (&delta.x)[axis];
    Vector3 before = pos;
    if (sweepAxis(world, self, pos, half, axis, d) && wasGrounded && stepOffset > 0) {
      Vector3 up = before;
      if (!sweepAxis(world, self, up, half, 1, stepOffset)) {
        Vector3 stepped = up;
        if (!sweepAxis(world, self, stepped, half, axis, d)) pos = stepped;
      }
    }
  }
  // Vertical.
  if (sweepAxis(world, self, pos, half, 1, delta.y)) {
    if (delta.y < 0) grounded = true;
    velocity.y = 0;
  }
  t->setPosition(pos);
}

namespace physics {

void resetTriggerMemory() {
  for (TriggerMemory& m : g_memory) m = TriggerMemory();
}

void fixedStep(World& world, Scalar, void*) {
  // Collect controllers (bounded: bit mask of 32).
  struct Ctl { GameObject* go; Vector3 pos; Vector3 half; };
  Ctl ctls[32];
  u32 nctl = 0;
  world.forEach<CharacterController>([&](CharacterController& c) {
    GameObject* go = c.gameObject();
    if (nctl >= 32 || !go || !go->activeInHierarchy() || !c.enabled()) return;
    ctls[nctl++] = {go, go->transform.position(), Vector3(c.radius, c.height * 0.5f, c.radius)};
  });
  if (!nctl) return;

  auto process = [&](GameObject* trig, const bool hit[32]) {
    if (trig->index >= 1024) return;
    TriggerMemory& m = g_memory[trig->index];
    if (m.generation != trig->generation) { m.generation = trig->generation; m.mask = 0; }
    u32 now = 0;
    for (u32 i = 0; i < nctl; ++i)
      if (hit[i]) now |= 1u << i;
    u32 entered = now & ~m.mask, exited = m.mask & ~now;
    m.mask = now;
    for (u32 i = 0; i < nctl; ++i) {
      if (!((entered | exited) & (1u << i))) continue;
      const bool enter = (entered & (1u << i)) != 0;
      Ref<GameObject> tr = trig->ref(), pl = ctls[i].go->ref();
      const World::Contact kind = enter ? World::Contact::TriggerEnter : World::Contact::TriggerExit;
      if (tr) world.sendContact(tr.get(), ctls[i].go, kind);
      if (tr && pl) world.sendContact(pl.get(), tr.get(), kind);
    }
  };

  world.forEach<SphereCollider>([&](SphereCollider& s) {
    GameObject* go = s.gameObject();
    if (!s.isTrigger || !s.enabled() || !go || !go->activeInHierarchy()) return;
    Vector3 c = go->transform.position();
    Vector3 sc = worldScale(&go->transform);
    Scalar r = s.radius * Mathf::Max(Mathf::Abs(sc.x), Mathf::Max(Mathf::Abs(sc.y), Mathf::Abs(sc.z)));
    bool hit[32] = {};
    for (u32 i = 0; i < nctl; ++i) {
      Vector3 q(Mathf::Clamp(c.x, ctls[i].pos.x - ctls[i].half.x, ctls[i].pos.x + ctls[i].half.x),
                Mathf::Clamp(c.y, ctls[i].pos.y - ctls[i].half.y, ctls[i].pos.y + ctls[i].half.y),
                Mathf::Clamp(c.z, ctls[i].pos.z - ctls[i].half.z, ctls[i].pos.z + ctls[i].half.z));
      hit[i] = sqr(q.x - c.x) + sqr(q.y - c.y) + sqr(q.z - c.z) <= r * r;
    }
    process(go, hit);
  });
  world.forEach<BoxCollider>([&](BoxCollider& b) {
    GameObject* go = b.gameObject();
    if (!b.isTrigger || !b.enabled() || !go || !go->activeInHierarchy()) return;
    Aabb w = boxWorld(b);
    bool hit[32] = {};
    for (u32 i = 0; i < nctl; ++i) hit[i] = overlaps(w, around(ctls[i].pos, ctls[i].half));
    process(go, hit);
  });
}

}  // namespace physics
}  // namespace rensga
