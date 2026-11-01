// F6.08 (A2): the fixed 3D physics step. Bodies are Rigidbody objects (dynamic or kinematic) and CharacterControllers (kinematic, moved
// by their own Move). Order of one step:
//   1 gather bodies and colliders (component pool order)        5 sequential impulses (normal + friction, speculative contacts)
//   2 forces: gravity, AddForce, drag, kinematic velocity       6 integrate, then split position correction of the penetration
//   3 broadphase over swept bounds                               7 contacts re-measured at the final poses -> Enter / Stay / Exit
//   4 narrowphase, one contact per collider pair                 8 transforms written, sleeping, callbacks dispatched (after the step)
// No rotational dynamics: bodies never rotate, so a single contact point per pair is enough.
#include "physics3d_internal.h"

#include "rensga/core/components.h"
#include "rensga/core/physics_components.h"

namespace rensga {
namespace physics3d {

namespace {

constexpr Scalar kTouch = 0.005f;         // surfaces closer than this count as touching (callbacks)
constexpr Scalar kSlop = 0.01f;           // penetration tolerated before the position correction pushes
constexpr Scalar kMaxSpeed = 200.0f;
constexpr Scalar kMaxKinematicSpeed = 50.0f;
constexpr Scalar kTeleport = 5.0f;        // a transform moved farther than this in one step is a teleport, not a velocity
constexpr u32 kMaxPairs = 256;

inline Scalar dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

enum class Kind : u8 { Dynamic, KinMove, KinFollow };

struct Body {
  GameObject* go = nullptr;
  Rigidbody* rb = nullptr;
  Vector3 start, v, v0, disp;
  Vector3 target;  // KinMove: where MovePosition puts the body
  Scalar invMass = 0;
  Kind kind = Kind::KinFollow;
  u8 frozen = 0;
  bool sleeping = false;
  bool moves = false;  // integrates v * dt
  u16 touching = 0;
};

struct Contact {
  u16 a = 0, b = 0;      // collider indices (a ranks <= b)
  i16 ba = -1, bb = -1;  // bodies
  Manifold m;
  Scalar jn = 0, K = 0, bounce = 0;
  Vector3 jt;
  bool solid = false;    // takes part in the solver (a solid pair with some mass)
  bool trigger = false;
  bool legacy = false;
};

struct PairKey {
  u32 a, aGen, b, bGen;
  u8 trigger;
};
int cmpKey(const PairKey& x, const PairKey& y) {
  if (x.trigger != y.trigger) return x.trigger < y.trigger ? -1 : 1;
  if (x.a != y.a) return x.a < y.a ? -1 : 1;
  if (x.b != y.b) return x.b < y.b ? -1 : 1;
  if (x.aGen != y.aGen) return x.aGen < y.aGen ? -1 : 1;
  if (x.bGen != y.bGen) return x.bGen < y.bGen ? -1 : 1;
  return 0;
}
struct PairInfo {
  PairKey key;
  bool legacy = false;
  Vector3 rel;       // velocity of the lower object relative to the other (before the solve)
  Vector3 impulse;   // normal impulse along n (a -> b)
  Vector3 point[kMaxContactPoints];
  Vector3 normal[kMaxContactPoints];  // n, a -> b
  u32 count = 0;
};

struct Mem {
  u32 gen = 0;
  Vector3 prev;
  bool valid = false;
};

Col g_cols[kMaxColliders];
Body g_bodies[kMaxBodies];
Contact g_contacts[kMaxContacts];
i16 g_bodyOf[kMaxTrackedObjects];
Mem g_mem[kMaxTrackedObjects];
PairInfo g_cur[kMaxPairs];
PairKey g_prev[kMaxPairs];
bool g_prevLegacy[kMaxPairs];
u32 g_prevCount = 0;
Stats g_stats;

}  // namespace

const Stats& stats() { return g_stats; }

void resetState() {
  g_prevCount = 0;
  for (Mem& m : g_mem) m = Mem();
}

namespace {

void wakeBody(Body& b) {
  b.sleeping = false;
  if (b.rb) { b.rb->sleeping_ = false; b.rb->sleepTicks_ = 0; }
}

Scalar invMassOf(const Body& b) { return b.invMass; }

void buildEvents(World& world, u32 nContacts, const Body* bodies, Scalar dt, u32* curCount) {
  (void)world;
  (void)dt;
  u32 n = 0;
  for (u32 i = 0; i < nContacts; ++i) {
    const Contact& c = g_contacts[i];
    const Col& A = g_cols[c.a];
    const Col& B = g_cols[c.b];
    // Final pose: bodies moved, so the colliders follow.
    Col a = A, b = B;
    if (a.body >= 0) a.c = bodies[a.body].go->transform.position() + a.off;
    if (b.body >= 0) b.c = bodies[b.body].go->transform.position() + b.off;
    Manifold m;
    if (!collide(a, b, kMargin, &m)) continue;
    const bool trigger = a.trigger || b.trigger;
    if (trigger ? !(m.sep < 0) : !(m.sep < kTouch)) continue;
    // A pair only produces callbacks when a body is involved (a Rigidbody or a CharacterController).
    if (a.body < 0 && b.body < 0) continue;
    PairKey key;
    const bool swap = a.goIndex > b.goIndex;
    key.a = swap ? b.goIndex : a.goIndex;
    key.aGen = swap ? b.goGen : a.goGen;
    key.b = swap ? a.goIndex : b.goIndex;
    key.bGen = swap ? a.goGen : b.goGen;
    key.trigger = trigger ? 1 : 0;
    // Find or insert in sorted order.
    u32 pos = 0, hi = n;
    while (pos < hi) {
      const u32 mid = (pos + hi) / 2;
      if (cmpKey(g_cur[mid].key, key) < 0) pos = mid + 1; else hi = mid;
    }
    const bool found = pos < n && cmpKey(g_cur[pos].key, key) == 0;
    Vector3 nrm = swap ? -m.n : m.n;  // from the lower-index object toward the other
    if (!found) {
      if (n >= kMaxPairs) { ++g_stats.dropped; continue; }
      for (u32 k = n; k > pos; --k) g_cur[k] = g_cur[k - 1];
      ++n;
      PairInfo& p = g_cur[pos];
      p = PairInfo();
      p.key = key;
      p.legacy = c.legacy;
      const Vector3 va = a.body >= 0 ? bodies[a.body].v0 : Vector3();
      const Vector3 vb = b.body >= 0 ? bodies[b.body].v0 : Vector3();
      p.rel = swap ? (vb - va) : (va - vb);
      p.impulse = nrm * c.jn;
    }
    PairInfo& p = g_cur[pos];
    if (p.count < kMaxContactPoints) {
      p.point[p.count] = m.p;
      p.normal[p.count] = nrm;
      ++p.count;
    }
  }
  *curCount = n;
}

void dispatch(World& world, u32 curCount) {
  // Copy the previous keys: the callbacks may destroy objects and call queries, never this step's tables.
  PairKey prev[kMaxPairs];
  bool prevLegacy[kMaxPairs];
  const u32 prevCount = g_prevCount;
  for (u32 i = 0; i < prevCount; ++i) { prev[i] = g_prev[i]; prevLegacy[i] = g_prevLegacy[i]; }
  // Remember this step's pairs first.
  for (u32 i = 0; i < curCount; ++i) { g_prev[i] = g_cur[i].key; g_prevLegacy[i] = g_cur[i].legacy; }
  g_prevCount = curCount;

  auto resolve = [&](u32 idx, u32 gen) -> GameObject* {
    GameObject* go = world.objectAt(idx);
    return (go && go->generation == gen) ? go : nullptr;
  };
  auto send = [&](const PairKey& k, const PairInfo* info, int kind /*0 enter 1 stay 2 exit*/, bool legacy) {
    GameObject* ga = resolve(k.a, k.aGen);
    GameObject* gb = resolve(k.b, k.bGen);
    if (!ga || !gb) return;
    Ref<GameObject> ra = ga->ref(), rb = gb->ref();
    if (k.trigger) {
      using C = World::Contact;
      if (kind == 1) {
        if (ra) world.sendContact(ra.get(), gb, C::TriggerStay);
        if (ra && rb) world.sendContact(rb.get(), ra.get(), C::TriggerStay);
      } else if (!legacy) {
        const C kc = kind == 0 ? C::TriggerEnter : C::TriggerExit;
        if (ra) world.sendContact(ra.get(), gb, kc);
        if (ra && rb) world.sendContact(rb.get(), ra.get(), kc);
      }
      return;
    }
    using CK = World::CollisionKind;
    const CK ck = kind == 0 ? CK::Enter : (kind == 1 ? CK::Stay : CK::Exit);
    Collision cola, colb;  // cola goes to the lower-index object `a`, colb to `b`
    cola.gameObject = gb;
    colb.gameObject = ga;
    if (info) {
      cola.relativeVelocity = info->rel;
      colb.relativeVelocity = -info->rel;
      cola.impulse = -info->impulse;
      colb.impulse = info->impulse;
      cola.contactCount = colb.contactCount = info->count;
      for (u32 j = 0; j < info->count; ++j) {
        cola.contacts[j].point = colb.contacts[j].point = info->point[j];
        cola.contacts[j].normal = -info->normal[j];  // from the other collider toward this one
        colb.contacts[j].normal = info->normal[j];
      }
    }
    if (ra) world.sendCollision(ra.get(), cola, ck);
    if (ra && rb) world.sendCollision(rb.get(), colb, ck);
  };

  // Enter / Stay for the current pairs (sorted order), then Exit for the vanished previous pairs.
  u32 pi = 0;
  for (u32 i = 0; i < curCount; ++i) {
    while (pi < prevCount && cmpKey(prev[pi], g_cur[i].key) < 0) ++pi;
    const bool was = pi < prevCount && cmpKey(prev[pi], g_cur[i].key) == 0;
    PairInfo info = g_cur[i];  // callbacks may run queries: keep a copy, not a reference into the table
    send(info.key, &info, was ? 1 : 0, info.legacy);
    if (was) ++g_stats.stays; else ++g_stats.enters;
  }
  u32 ci = 0;
  for (u32 i = 0; i < prevCount; ++i) {
    // g_cur was overwritten into g_prev already, but g_cur itself is intact until the next step.
    while (ci < curCount && cmpKey(g_cur[ci].key, prev[i]) < 0) ++ci;
    if (ci < curCount && cmpKey(g_cur[ci].key, prev[i]) == 0) continue;
    send(prev[i], nullptr, 2, prevLegacy[i]);
    ++g_stats.exits;
  }
}

}  // namespace

void step(World& world, Scalar dt) {
  g_stats = Stats();
  if (!(dt > 0)) return;
  const Settings& S = settings();

  // ---- 1. bodies
  u32 nb = 0;
  for (u32 i = 0; i < kMaxTrackedObjects; ++i) g_bodyOf[i] = -1;
  const bool hasRb = world.pool<Rigidbody>() && world.pool<Rigidbody>()->liveCount > 0;
  const bool hasCc = world.pool<CharacterController>() && world.pool<CharacterController>()->liveCount > 0;
  if (!hasRb && !hasCc && g_prevCount == 0) return;  // nothing to simulate and nothing to close: scenes without 3D bodies cost nothing
  world.forEach<Rigidbody>([&](Rigidbody& rb) {
    GameObject* go = rb.gameObject();
    if (!rb.enabled() || !go || !go->activeInHierarchy() || go->index >= kMaxTrackedObjects) return;
    if (nb >= kMaxBodies) { ++g_stats.dropped; return; }
    Body& b = g_bodies[nb];
    b = Body();
    b.go = go;
    b.rb = &rb;
    b.start = go->transform.position();
    b.frozen = static_cast<u8>(rb.constraints & 7);
    b.sleeping = rb.sleeping_;
    if (rb.isKinematic) {
      b.kind = rb.hasMove_ ? Kind::KinMove : Kind::KinFollow;
      b.invMass = 0;
    } else {
      b.kind = Kind::Dynamic;
      b.invMass = 1.0f / Mathf::Max(rb.mass, 0.001f);
      if (rb.hasMove_) { b.start = rb.moveTarget_; go->transform.setPosition(b.start); }  // MovePosition on a dynamic body teleports it
    }
    g_bodyOf[go->index] = static_cast<i16>(nb++);
  });
  world.forEach<CharacterController>([&](CharacterController& cc) {
    GameObject* go = cc.gameObject();
    if (!cc.enabled() || !go || !go->activeInHierarchy() || go->index >= kMaxTrackedObjects || g_bodyOf[go->index] >= 0) return;
    if (nb >= kMaxBodies) { ++g_stats.dropped; return; }
    Body& b = g_bodies[nb];
    b = Body();
    b.go = go;
    b.start = go->transform.position();
    b.kind = Kind::KinFollow;
    g_bodyOf[go->index] = static_cast<i16>(nb++);
  });
  g_stats.bodies = nb;

  // ---- 2. colliders and velocities
  u32 dropped = 0;
  const u32 nc = gatherColliders(world, g_cols, kMaxColliders, &dropped);
  g_stats.colliders = nc;
  g_stats.dropped += dropped;
  for (u32 i = 0; i < nc; ++i) {
    Col& c = g_cols[i];
    for (const Transform* t = &c.go->transform; t; t = t->parent_) {
      const GameObject* g = t->gameObject;
      if (g && g->index < kMaxTrackedObjects && g_bodyOf[g->index] >= 0) { c.body = g_bodyOf[g->index]; break; }
    }
    if (c.body >= 0) c.off = c.c - g_bodies[c.body].start;
  }

  const Scalar thr2 = S.sleepThreshold * S.sleepThreshold;
  for (u32 i = 0; i < nb; ++i) {
    Body& b = g_bodies[i];
    if (b.kind == Kind::Dynamic) {
      Rigidbody& rb = *b.rb;
      b.v = rb.velocity_;
      if (b.sleeping) b.v = Vector3();
      else {
        if (rb.useGravity) b.v += S.gravity * dt;
        b.v += rb.force_ * dt + rb.impulse_;
        if (rb.drag > 0) b.v = b.v / (1.0f + rb.drag * dt);
        const Scalar sp2 = b.v.sqrMagnitude();
        if (sp2 > kMaxSpeed * kMaxSpeed) b.v = b.v * (kMaxSpeed / Mathf::Sqrt(sp2));
      }
      rb.force_ = Vector3();
      rb.impulse_ = Vector3();
      if (b.frozen & 1) b.v.x = 0;
      if (b.frozen & 2) b.v.y = 0;
      if (b.frozen & 4) b.v.z = 0;
      b.moves = !b.sleeping;
    } else {
      Mem& m = g_mem[b.go->index];
      const Vector3 now = b.start;
      Vector3 kv;
      if (b.kind == Kind::KinMove) {
        b.target = b.rb->moveTarget_;
        kv = (b.target - now) / dt;
        b.moves = true;
      } else if (m.valid && m.gen == b.go->generation) {
        const Vector3 d = now - m.prev;
        if (d.sqrMagnitude() <= kTeleport * kTeleport) kv = d / dt;
      }
      const Scalar sp2 = kv.sqrMagnitude();
      if (sp2 > kMaxKinematicSpeed * kMaxKinematicSpeed) kv = kv * (kMaxKinematicSpeed / Mathf::Sqrt(sp2));
      b.v = kv;
      b.sleeping = false;
    }
    b.v0 = b.v;
  }

  // ---- 3/4. pairs
  u32 ncon = 0;
  auto swept = [&](const Col& c, Vector3* lo, Vector3* hi) {
    *lo = c.lo - Vector3(kMargin, kMargin, kMargin);
    *hi = c.hi + Vector3(kMargin, kMargin, kMargin);
    if (c.body >= 0 && g_bodies[c.body].moves) {
      const Vector3 d = g_bodies[c.body].v * dt;
      if (d.x < 0) lo->x += d.x; else hi->x += d.x;
      if (d.y < 0) lo->y += d.y; else hi->y += d.y;
      if (d.z < 0) lo->z += d.z; else hi->z += d.z;
    }
  };
  for (u32 i = 0; i < nc; ++i) {
    Vector3 li, hi_;
    swept(g_cols[i], &li, &hi_);
    for (u32 j = i + 1; j < nc; ++j) {
      const Col& ci = g_cols[i];
      const Col& cj = g_cols[j];
      if (ci.body < 0 && cj.body < 0) continue;
      if (ci.body == cj.body || ci.goIndex == cj.goIndex) continue;
      if (!layersCollide(ci.layer, cj.layer)) continue;
      Vector3 lj, hj;
      swept(cj, &lj, &hj);
      if (li.x > hj.x || hi_.x < lj.x || li.y > hj.y || hi_.y < lj.y || li.z > hj.z || hi_.z < lj.z) continue;
      const bool swap = ci.type > cj.type;
      const Col& A = swap ? cj : ci;
      const Col& B = swap ? ci : cj;
      Manifold m;
      if (!collide(A, B, kMargin, &m)) continue;
      if (ncon >= kMaxContacts) { ++g_stats.dropped; continue; }
      Contact& c = g_contacts[ncon++];
      c = Contact();
      c.a = static_cast<u16>(swap ? j : i);
      c.b = static_cast<u16>(swap ? i : j);
      c.ba = static_cast<i16>(A.body);
      c.bb = static_cast<i16>(B.body);
      c.m = m;
      c.trigger = A.trigger || B.trigger;
      // A CharacterController against a Box/Sphere trigger: physics.cpp keeps sending Enter / Exit, this step adds Stay.
      c.legacy = c.trigger && ((A.proxy && (B.type == Shape::Box || B.type == Shape::Sphere)) ||
                               (B.proxy && (A.type == Shape::Box || A.type == Shape::Sphere)));
      if (c.trigger) continue;
      const Body* ba = c.ba >= 0 ? &g_bodies[c.ba] : nullptr;
      const Body* bb = c.bb >= 0 ? &g_bodies[c.bb] : nullptr;
      const Scalar ia = ba ? invMassOf(*ba) : 0, ib = bb ? invMassOf(*bb) : 0;
      c.K = ia + ib;
      c.solid = c.K > 0;
      // A moving body wakes a sleeper it touches.
      if (ba && bb && c.m.sep < kTouch) {
        Body* x = const_cast<Body*>(ba);
        Body* y = const_cast<Body*>(bb);
        if (x->sleeping && !y->sleeping && y->v.sqrMagnitude() > thr2) wakeBody(*x);
        if (y->sleeping && !x->sleeping && x->v.sqrMagnitude() > thr2) wakeBody(*y);
      }
    }
  }
  g_stats.pairs = ncon;

  // ---- 5. velocity solver
  for (u32 i = 0; i < ncon; ++i) {
    Contact& c = g_contacts[i];
    if (!c.solid) continue;
    Body* ba = c.ba >= 0 ? &g_bodies[c.ba] : nullptr;
    Body* bb = c.bb >= 0 ? &g_bodies[c.bb] : nullptr;
    const Vector3 va = ba ? ba->v : Vector3(), vb = bb ? bb->v : Vector3();
    const Scalar vn0 = dot(vb - va, c.m.n);
    if (S.bounciness > 0 && vn0 < -S.bounceThreshold && c.m.sep < kTouch) c.bounce = -vn0 * S.bounciness;
  }
  for (u32 it = 0; it < kSolverIterations; ++it) {
    for (u32 i = 0; i < ncon; ++i) {
      Contact& c = g_contacts[i];
      if (!c.solid) continue;
      Body* ba = c.ba >= 0 ? &g_bodies[c.ba] : nullptr;
      Body* bb = c.bb >= 0 ? &g_bodies[c.bb] : nullptr;
      const Scalar ia = ba ? ba->invMass : 0, ib = bb ? bb->invMass : 0;
      const Vector3& n = c.m.n;
      Vector3 va = ba ? ba->v : Vector3(), vb = bb ? bb->v : Vector3();
      const Scalar vn = dot(vb - va, n);
      const Scalar target = c.m.sep > 0 ? -c.m.sep / dt : c.bounce;
      Scalar lam = (target - vn) / c.K;
      Scalar acc = c.jn + lam;
      if (acc < 0) acc = 0;
      lam = acc - c.jn;
      c.jn = acc;
      if (ba) ba->v -= n * (lam * ia);
      if (bb) bb->v += n * (lam * ib);
      if (S.friction > 0 && c.m.sep < kTouch && c.jn > 0) {
        va = ba ? ba->v : Vector3();
        vb = bb ? bb->v : Vector3();
        const Vector3 rv = vb - va;
        const Vector3 vt = rv - n * dot(rv, n);
        Vector3 nj = c.jt - vt / c.K;
        const Scalar maxF = S.friction * c.jn;
        const Scalar m2 = nj.sqrMagnitude();
        if (m2 > maxF * maxF) nj = nj * (maxF / Mathf::Sqrt(m2));
        const Vector3 dj = nj - c.jt;
        c.jt = nj;
        if (ba) ba->v -= dj * ia;
        if (bb) bb->v += dj * ib;
      }
    }
    for (u32 k = 0; k < nb; ++k) {
      Body& b = g_bodies[k];
      if (b.frozen & 1) b.v.x = 0;
      if (b.frozen & 2) b.v.y = 0;
      if (b.frozen & 4) b.v.z = 0;
    }
  }

  // ---- 6. position correction (split from the velocity solve: the corrections never add velocity)
  for (u32 it = 0; it < kPositionIterations; ++it) {
    for (u32 i = 0; i < ncon; ++i) {
      Contact& c = g_contacts[i];
      if (!c.solid) continue;
      Body* ba = c.ba >= 0 ? &g_bodies[c.ba] : nullptr;
      Body* bb = c.bb >= 0 ? &g_bodies[c.bb] : nullptr;
      const Vector3 da = ba ? ba->disp + (ba->moves ? ba->v * dt : Vector3()) : Vector3();
      const Vector3 db = bb ? bb->disp + (bb->moves ? bb->v * dt : Vector3()) : Vector3();
      const Scalar sep = c.m.sep + dot(db - da, c.m.n);
      if (sep >= -kSlop) continue;
      const Scalar corr = (-sep - kSlop) * 0.8f;
      if (ba && ba->kind == Kind::Dynamic) ba->disp -= c.m.n * (corr * ba->invMass / c.K);
      if (bb && bb->kind == Kind::Dynamic) bb->disp += c.m.n * (corr * bb->invMass / c.K);
    }
  }

  // ---- 7/8. integrate and write back
  for (u32 i = 0; i < nb; ++i) {
    Body& b = g_bodies[i];
    if (b.kind == Kind::Dynamic) {
      Rigidbody& rb = *b.rb;
      const Scalar sp2 = b.v.sqrMagnitude();
      if (b.sleeping) {
        if (sp2 > thr2) wakeBody(b);
        else b.v = Vector3();
      }
      Vector3 np = b.start + b.disp;
      if (!b.sleeping) np += b.v * dt;
      if (b.frozen & 1) np.x = b.start.x;
      if (b.frozen & 2) np.y = b.start.y;
      if (b.frozen & 4) np.z = b.start.z;
      if (np != b.start) b.go->transform.setPosition(np);
      rb.velocity_ = b.v;
      b.start = np;  // final pose for the contact re-measure (equal to the transform's position, up to parent scale)
    } else if (b.kind == Kind::KinMove) {
      b.go->transform.setPosition(b.target);
      b.rb->velocity_ = b.v;
      b.rb->hasMove_ = false;
    } else if (b.rb) {
      b.rb->velocity_ = b.v;
    }
  }
  // Kinematic history for the next step's velocity.
  for (u32 i = 0; i < nb; ++i) {
    Body& b = g_bodies[i];
    if (b.kind == Kind::Dynamic) continue;
    Mem& m = g_mem[b.go->index];
    m.gen = b.go->generation;
    m.prev = b.go->transform.position();
    m.valid = true;
  }

  // ---- 9. touching counts, sleeping, events
  u32 nev = 0;
  buildEvents(world, ncon, g_bodies, dt, &nev);
  for (u32 i = 0; i < ncon; ++i) {
    const Contact& c = g_contacts[i];
    if (!c.solid || !(c.m.sep < kTouch)) continue;
    if (c.ba >= 0) ++g_bodies[c.ba].touching;
    if (c.bb >= 0) ++g_bodies[c.bb].touching;
  }
  for (u32 i = 0; i < nb; ++i) {
    Body& b = g_bodies[i];
    if (b.kind != Kind::Dynamic) continue;
    Rigidbody& rb = *b.rb;
    if (b.sleeping) {
      if (b.touching == 0) wakeBody(b);  // lost its support
      continue;
    }
    if (b.v.sqrMagnitude() <= thr2 && (b.touching > 0 || !rb.useGravity)) {
      if (rb.sleepTicks_ < 255) ++rb.sleepTicks_;
      if (rb.sleepTicks_ >= kSleepSteps) { rb.sleeping_ = true; rb.velocity_ = Vector3(); }
    } else {
      rb.sleepTicks_ = 0;
    }
  }
  g_stats.contacts = ncon;
  dispatch(world, nev);
}

}  // namespace physics3d
}  // namespace rensga
