// F6.08 (A2): colliders in world space, shape pair tests (contact normal, penetration) and the ray / cast / overlap queries.
// Scalar-only arithmetic (add, sub, mul, div, sqrt): no libm, so a PSP build and a PC build give the same bits.
#include "physics3d_internal.h"

#include "rensga/core/components.h"
#include "rensga/core/physics_components.h"

namespace rensga {
namespace physics3d {

namespace {

constexpr Scalar kBig = 3.0e38f;
constexpr Scalar kEps = 1e-6f;

inline Scalar dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Scalar absS(Scalar v) { return v < 0 ? -v : v; }
inline Scalar maxAbs3(const Vector3& s) { return Mathf::Max(absS(s.x), Mathf::Max(absS(s.y), absS(s.z))); }
inline Vector3 comp(const Vector3& v, const Vector3& s) { return Vector3(v.x * s.x, v.y * s.y, v.z * s.z); }
inline Scalar at(const Vector3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

Settings g_settings;

}  // namespace

Settings& settings() { return g_settings; }
void resetSettings() { g_settings = Settings(); }
void loadFromPack(const PackView* pack) {
  resetSettings();
  if (!pack) return;
  const PackPhysics3D* ph = pack->content().physics3d;
  if (!ph) return;
  g_settings.gravity = Vector3(ph->gravity[0], ph->gravity[1], ph->gravity[2]);
  g_settings.queriesHitTriggers = (ph->flags & kPhy3QueriesHitTriggers) != 0;
  for (u32 i = 0; i < 32; ++i) g_settings.masks[i] = ph->masks[i];
  if (ph->bounceThreshold > 0) g_settings.bounceThreshold = ph->bounceThreshold;
  if (ph->sleepThreshold > 0) g_settings.sleepThreshold = ph->sleepThreshold;
}
bool layersCollide(u32 a, u32 b) { return ((g_settings.masks[a & 31u] >> (b & 31u)) & 1u) != 0; }
void setIgnoreLayerCollision(u32 a, u32 b, bool ignore) {
  if (a >= 32 || b >= 32) return;
  if (ignore) { g_settings.masks[a] &= ~(1u << b); g_settings.masks[b] &= ~(1u << a); }
  else { g_settings.masks[a] |= 1u << b; g_settings.masks[b] |= 1u << a; }
}

bool layerOk(const QueryFilter& f, const Col& c) {
  if (!((f.layerMask >> (c.layer & 31u)) & 1u)) return false;
  if (c.trigger && !f.triggers) return false;
  return true;
}

// ---- gathering ----------------------------------------------------------------------------------------------------------

void computeBounds(Col& c) {
  Vector3 e;
  if (c.type == Shape::Box) {
    e = Vector3(c.h.x * absS(c.ax[0].x) + c.h.y * absS(c.ax[1].x) + c.h.z * absS(c.ax[2].x),
                c.h.x * absS(c.ax[0].y) + c.h.y * absS(c.ax[1].y) + c.h.z * absS(c.ax[2].y),
                c.h.x * absS(c.ax[0].z) + c.h.y * absS(c.ax[1].z) + c.h.z * absS(c.ax[2].z));
  } else if (c.type == Shape::Sphere) {
    e = Vector3(c.r, c.r, c.r);
  } else {
    e = Vector3(absS(c.dir.x) * c.hl + c.r, absS(c.dir.y) * c.hl + c.r, absS(c.dir.z) * c.hl + c.r);
  }
  c.lo = c.c - e;
  c.hi = c.c + e;
}

u32 gatherColliders(World& world, Col* out, u32 capacity, u32* dropped) {
  u32 n = 0, drop = 0;
  auto begin = [&](Component& comp, bool trigger, Col*& slot) -> bool {
    GameObject* go = comp.gameObject();
    if (!comp.enabled() || !go || !go->activeInHierarchy()) return false;
    if (n >= capacity) { ++drop; return false; }
    slot = &out[n];
    *slot = Col();
    slot->trigger = trigger;
    slot->go = go;
    slot->goIndex = go->index;
    slot->goGen = go->generation;
    slot->layer = static_cast<u8>(go->layer & 31u);
    return true;
  };
  world.forEach<BoxCollider>([&](BoxCollider& b) {
    Col* c;
    if (!begin(b, b.isTrigger, c)) return;
    const Transform& t = c->go->transform;
    const Vector3 pos = t.position(), s = t.lossyScale();
    const Quaternion q = t.rotation();
    c->type = Shape::Box;
    c->c = pos + q * comp(b.center, s);
    c->h = Vector3(absS(b.size.x * s.x) * 0.5f, absS(b.size.y * s.y) * 0.5f, absS(b.size.z * s.z) * 0.5f);
    c->ax[0] = q * Vector3(1, 0, 0);
    c->ax[1] = q * Vector3(0, 1, 0);
    c->ax[2] = q * Vector3(0, 0, 1);
    computeBounds(*c);
    ++n;
  });
  world.forEach<SphereCollider>([&](SphereCollider& s) {
    Col* c;
    if (!begin(s, s.isTrigger, c)) return;
    const Transform& t = c->go->transform;
    const Vector3 pos = t.position(), sc = t.lossyScale();
    c->type = Shape::Sphere;
    c->c = pos + t.rotation() * comp(s.center, sc);
    c->r = s.radius * maxAbs3(sc);
    computeBounds(*c);
    ++n;
  });
  world.forEach<CapsuleCollider>([&](CapsuleCollider& k) {
    Col* c;
    if (!begin(k, k.isTrigger, c)) return;
    const Transform& t = c->go->transform;
    const Vector3 pos = t.position(), sc = t.lossyScale();
    const Quaternion q = t.rotation();
    const i32 axis = k.direction < 0 ? 0 : (k.direction > 2 ? 2 : k.direction);
    const Scalar sa = absS(at(sc, axis));
    const Scalar sp = Mathf::Max(absS(at(sc, (axis + 1) % 3)), absS(at(sc, (axis + 2) % 3)));
    c->type = Shape::Capsule;
    c->c = pos + q * comp(k.center, sc);
    c->r = absS(k.radius) * sp;
    const Scalar half = absS(k.height) * sa * 0.5f - c->r;
    c->hl = half > 0 ? half : 0;
    c->dir = q * Vector3(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f);
    computeBounds(*c);
    ++n;
  });
  world.forEach<CharacterController>([&](CharacterController& cc) {
    Col* c;
    if (!begin(cc, false, c)) return;
    c->type = Shape::Box;
    c->proxy = true;
    c->c = c->go->transform.position();
    c->h = Vector3(cc.radius, cc.height * 0.5f, cc.radius);
    c->ax[0] = Vector3(1, 0, 0);
    c->ax[1] = Vector3(0, 1, 0);
    c->ax[2] = Vector3(0, 0, 1);
    computeBounds(*c);
    ++n;
  });
  if (dropped) *dropped = drop;
  return n;
}

// ---- signed distance ----------------------------------------------------------------------------------------------------

namespace {

Scalar sdBox(const Col& b, const Vector3& p, Vector3* closest, Vector3* normal) {
  const Vector3 d = p - b.c;
  const Scalar l[3] = {dot(d, b.ax[0]), dot(d, b.ax[1]), dot(d, b.ax[2])};
  const Scalar h[3] = {b.h.x, b.h.y, b.h.z};
  bool inside = true;
  Scalar q[3];
  for (int i = 0; i < 3; ++i) {
    q[i] = Mathf::Clamp(l[i], -h[i], h[i]);
    if (absS(l[i]) > h[i]) inside = false;
  }
  if (!inside) {
    const Vector3 cp = b.c + b.ax[0] * q[0] + b.ax[1] * q[1] + b.ax[2] * q[2];
    const Vector3 e = p - cp;
    const Scalar sd = e.magnitude();
    if (closest) *closest = cp;
    if (normal) *normal = sd > kEps ? e / sd : b.ax[1];
    return sd;
  }
  int best = 0;
  Scalar bd = h[0] - absS(l[0]);
  for (int i = 1; i < 3; ++i) {
    const Scalar di = h[i] - absS(l[i]);
    if (di < bd) { bd = di; best = i; }
  }
  const Vector3 nrm = l[best] >= 0 ? b.ax[best] : -b.ax[best];
  if (normal) *normal = nrm;
  if (closest) *closest = p + nrm * bd;
  return -bd;
}

Vector3 segPoint(const Col& c, const Vector3& p) {  // closest point of the capsule's core segment to p
  Scalar s = dot(p - c.c, c.dir);
  s = Mathf::Clamp(s, -c.hl, c.hl);
  return c.c + c.dir * s;
}

Scalar sdSphereLike(const Vector3& core, Scalar r, const Vector3& p, Vector3* closest, Vector3* normal) {
  const Vector3 d = p - core;
  const Scalar m = d.magnitude();
  const Vector3 n = m > kEps ? d / m : Vector3(0, 1, 0);
  if (normal) *normal = n;
  if (closest) *closest = core + n * r;
  return m - r;
}

}  // namespace

Scalar signedDistance(const Col& c, const Vector3& p, Vector3* closest, Vector3* normal) {
  switch (c.type) {
    case Shape::Box: return sdBox(c, p, closest, normal);
    case Shape::Sphere: return sdSphereLike(c.c, c.r, p, closest, normal);
    case Shape::Capsule: return sdSphereLike(segPoint(c, p), c.r, p, closest, normal);
  }
  return kBig;
}

// ---- pair tests ---------------------------------------------------------------------------------------------------------

namespace {

// Closest points of segments p1-q1 and p2-q2 (Ericson, Real-Time Collision Detection 5.1.9).
void closestSegSeg(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2, Vector3* c1, Vector3* c2) {
  const Vector3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
  const Scalar a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
  Scalar s, t;
  if (a <= kEps && e <= kEps) { *c1 = p1; *c2 = p2; return; }
  if (a <= kEps) {
    s = 0;
    t = Mathf::Clamp(f / e, 0.0f, 1.0f);
  } else {
    const Scalar c = dot(d1, r);
    if (e <= kEps) {
      t = 0;
      s = Mathf::Clamp(-c / a, 0.0f, 1.0f);
    } else {
      const Scalar b = dot(d1, d2);
      const Scalar denom = a * e - b * b;
      s = denom > 1e-8f ? Mathf::Clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
      t = (b * s + f) / e;
      if (t < 0) { t = 0; s = Mathf::Clamp(-c / a, 0.0f, 1.0f); }
      else if (t > 1) { t = 1; s = Mathf::Clamp((b - c) / a, 0.0f, 1.0f); }
    }
  }
  *c1 = p1 + d1 * s;
  *c2 = p2 + d2 * t;
}

// Sphere (center, radius) against any collider X. Result normal runs from the sphere to X.
bool sphereVsAny(const Vector3& sc, Scalar sr, const Col& x, Scalar margin, Manifold* m) {
  Vector3 cl, nm;
  const Scalar sd = signedDistance(x, sc, &cl, &nm);
  const Scalar sep = sd - sr;
  if (!(sep < margin)) return false;
  m->sep = sep;
  m->n = -nm;  // nm points from X toward the sphere
  m->p = cl;
  return true;
}

bool boxBox(const Col& a, const Col& b, Scalar margin, Manifold* m) {
  const Vector3 t = b.c - a.c;
  const Scalar ha[3] = {a.h.x, a.h.y, a.h.z}, hb[3] = {b.h.x, b.h.y, b.h.z};
  Scalar best = -kBig;
  Vector3 bestN(0, 1, 0);
  for (int k = 0; k < 15; ++k) {
    Vector3 l;
    bool cross = false;
    if (k < 3) l = a.ax[k];
    else if (k < 6) l = b.ax[k - 3];
    else {
      cross = true;
      l = Vector3::Cross(a.ax[(k - 6) / 3], b.ax[(k - 6) % 3]);
      const Scalar len2 = l.sqrMagnitude();
      if (len2 < 1e-6f) continue;
      l = l / Mathf::Sqrt(len2);
    }
    Scalar ra = 0, rb = 0;
    for (int i = 0; i < 3; ++i) {
      ra += ha[i] * absS(dot(a.ax[i], l));
      rb += hb[i] * absS(dot(b.ax[i], l));
    }
    const Scalar dist = dot(t, l);
    const Scalar sep = absS(dist) - ra - rb;
    if (sep >= margin) return false;
    if (sep > best + (cross ? 0.002f : 0.0f)) {
      best = sep;
      bestN = dist >= 0 ? l : -l;
    }
  }
  Vector3 pa = a.c, pb = b.c;
  for (int i = 0; i < 3; ++i) {
    const Scalar da = dot(a.ax[i], bestN), db = dot(b.ax[i], bestN);
    if (da > 0.01f) pa += a.ax[i] * ha[i]; else if (da < -0.01f) pa -= a.ax[i] * ha[i];
    if (db > 0.01f) pb -= b.ax[i] * hb[i]; else if (db < -0.01f) pb += b.ax[i] * hb[i];
  }
  m->n = bestN;
  m->sep = best;
  m->p = (pa + pb) * 0.5f;
  return true;
}

}  // namespace

bool collide(const Col& a, const Col& b, Scalar margin, Manifold* m) {
  if (a.type == Shape::Sphere) return sphereVsAny(a.c, a.r, b, margin, m);
  if (a.type == Shape::Capsule) {
    if (b.type == Shape::Capsule) {
      Vector3 c1, c2;
      closestSegSeg(a.c - a.dir * a.hl, a.c + a.dir * a.hl, b.c - b.dir * b.hl, b.c + b.dir * b.hl, &c1, &c2);
      const Vector3 d = c2 - c1;
      const Scalar len = d.magnitude();
      const Scalar sep = len - a.r - b.r;
      if (!(sep < margin)) return false;
      m->n = len > kEps ? d / len : Vector3(0, 1, 0);
      m->sep = sep;
      m->p = c1 + m->n * a.r;
      return true;
    }
    // Capsule vs box: the signed distance of the box along the core segment is convex, so a fixed-count search finds the closest point.
    Scalar lo = -a.hl, hi = a.hl;
    if (a.hl > 0) {
      for (int i = 0; i < 20; ++i) {
        const Scalar third = (hi - lo) / 3.0f;
        const Scalar m1 = lo + third, m2 = hi - third;
        if (signedDistance(b, a.c + a.dir * m1, nullptr, nullptr) < signedDistance(b, a.c + a.dir * m2, nullptr, nullptr)) hi = m2;
        else lo = m1;
      }
    }
    return sphereVsAny(a.c + a.dir * ((lo + hi) * 0.5f), a.r, b, margin, m);
  }
  return boxBox(a, b, margin, m);
}

// ---- rays ---------------------------------------------------------------------------------------------------------------

namespace {

bool aabbRay(const Col& c, const Vector3& o, const Vector3& d, Scalar maxT) {
  Scalar t0 = 0, t1 = maxT;
  const Scalar lo[3] = {c.lo.x, c.lo.y, c.lo.z}, hi[3] = {c.hi.x, c.hi.y, c.hi.z};
  const Scalar oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
  for (int i = 0; i < 3; ++i) {
    if (absS(dd[i]) < 1e-9f) { if (oo[i] < lo[i] || oo[i] > hi[i]) return false; continue; }
    Scalar a = (lo[i] - oo[i]) / dd[i], b = (hi[i] - oo[i]) / dd[i];
    if (a > b) { const Scalar s = a; a = b; b = s; }
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
    if (t0 > t1) return false;
  }
  return true;
}

// Ray against one collider. Colliders that contain the origin are not hit (Unity).
bool rayCollider(const Col& c, const Vector3& o, const Vector3& d, Scalar maxT, Scalar* tOut, Vector3* nOut) {
  if (signedDistance(c, o, nullptr, nullptr) <= 0) return false;
  if (c.type == Shape::Box) {
    const Vector3 rel = o - c.c;
    const Scalar h[3] = {c.h.x, c.h.y, c.h.z};
    Scalar tmin = -kBig, tmax = kBig;
    Vector3 nrm(0, 1, 0);
    for (int i = 0; i < 3; ++i) {
      const Scalar ol = dot(rel, c.ax[i]), dl = dot(d, c.ax[i]);
      if (absS(dl) < 1e-9f) { if (absS(ol) > h[i]) return false; continue; }
      const Scalar inv = 1.0f / dl;
      Scalar t1 = (-h[i] - ol) * inv, t2 = (h[i] - ol) * inv;
      const Vector3 enter = dl > 0 ? -c.ax[i] : c.ax[i];
      if (t1 > t2) { const Scalar s = t1; t1 = t2; t2 = s; }
      if (t1 > tmin) { tmin = t1; nrm = enter; }
      if (t2 < tmax) tmax = t2;
      if (tmin > tmax) return false;
    }
    if (tmin < 0 || tmin > maxT) return false;
    *tOut = tmin;
    *nOut = nrm;
    return true;
  }
  if (c.type == Shape::Sphere) {
    const Vector3 mm = o - c.c;
    const Scalar b = dot(mm, d), cc = dot(mm, mm) - c.r * c.r;
    if (b > 0) return false;
    const Scalar disc = b * b - cc;
    if (disc < 0) return false;
    const Scalar t = -b - Mathf::Sqrt(disc);
    if (t < 0 || t > maxT) return false;
    *tOut = t;
    *nOut = (o + d * t - c.c) / c.r;
    return true;
  }
  // Capsule: the cylinder part, then the two end spheres.
  const Vector3& u = c.dir;
  const Vector3 mm = o - c.c;
  const Scalar md = dot(mm, u), dd = dot(d, u);
  const Vector3 mp = mm - u * md, dp = d - u * dd;
  const Scalar a = dot(dp, dp), b = dot(mp, dp), cc = dot(mp, mp) - c.r * c.r;
  Scalar best = kBig;
  Vector3 bn(0, 1, 0);
  if (a > 1e-12f) {
    const Scalar disc = b * b - a * cc;
    if (disc >= 0) {
      const Scalar t = (-b - Mathf::Sqrt(disc)) / a;
      if (t >= 0 && t < best) {
        const Scalar y = md + dd * t;
        if (absS(y) <= c.hl) { best = t; bn = (mp + dp * t) / c.r; }
      }
    }
  }
  for (int e = 0; e < 2; ++e) {
    const Scalar end = e == 0 ? c.hl : -c.hl;
    const Vector3 ec = c.c + u * end;
    const Vector3 me = o - ec;
    const Scalar b2 = dot(me, d), c2 = dot(me, me) - c.r * c.r;
    const Scalar disc = b2 * b2 - c2;
    if (disc < 0) continue;
    const Scalar t = -b2 - Mathf::Sqrt(disc);
    if (t < 0 || t >= best) continue;
    const Scalar y = md + dd * t;
    if (e == 0 ? y >= c.hl : y <= -c.hl) { best = t; bn = (o + d * t - ec) / c.r; }
  }
  if (best > maxT) return false;
  *tOut = best;
  *nOut = bn;
  return true;
}

constexpr Scalar kCastCap = 1000.0f;

Scalar capRange(Scalar maxDistance) { return maxDistance > kCastCap ? kCastCap : maxDistance; }

struct Scratch {
  Col cols[kMaxColliders];
};
Scratch g_scratch;

}  // namespace


bool isLegacyAabb(World& world, const BoxCollider& box) {
  const GameObject* go = box.gameObject();
  const Quaternion q = go->transform.rotation();
  const Vector3 x = q * Vector3(1, 0, 0), y = q * Vector3(0, 1, 0), z = q * Vector3(0, 0, 1);
  if (!(absS(x.x) > 0.9999f && absS(y.y) > 0.9999f && absS(z.z) > 0.9999f)) return false;
  for (const Transform* t = &go->transform; t; t = t->parent_)
    if (world.getComponent<Rigidbody>(t->gameObject) != nullptr) return false;
  return true;
}

Vector3 resolveBox(World& world, const GameObject* self, const Vector3& posIn, const Vector3& half, Scalar minNormalY, bool* grounded,
                   Vector3* groundNormal) {
  Vector3 pos = posIn;
  const u32 n = gatherColliders(world, g_scratch.cols, kMaxColliders, nullptr);
  bool skip[kMaxColliders];
  u32 relevant = 0;
  for (u32 i = 0; i < n; ++i) {
    const Col& c = g_scratch.cols[i];
    bool legacy = false;
    if (c.proxy || c.trigger || c.go == self) legacy = true;
    else if (c.type == Shape::Box) {
      // The old axis-aligned sweep already handles an unrotated box that no Rigidbody drives.
      const bool aligned = absS(c.ax[0].x) > 0.9999f && absS(c.ax[1].y) > 0.9999f && absS(c.ax[2].z) > 0.9999f;
      bool rb = false;
      if (aligned) for (const Transform* t = &c.go->transform; t && !rb; t = t->parent_) rb = world.getComponent<Rigidbody>(t->gameObject) != nullptr;
      legacy = aligned && !rb;
    }
    skip[i] = legacy;
    if (!legacy) ++relevant;
  }
  if (!relevant) return pos;
  Col me;
  me.type = Shape::Box;
  me.h = half;
  me.ax[0] = Vector3(1, 0, 0);
  me.ax[1] = Vector3(0, 1, 0);
  me.ax[2] = Vector3(0, 0, 1);
  const u32 mine = self ? (static_cast<u32>(self->layer) & 31u) : 0u;
  for (int it = 0; it < 4; ++it) {
    bool moved = false;
    for (u32 i = 0; i < n; ++i) {
      if (skip[i]) continue;
      const Col& c = g_scratch.cols[i];
      if (!layersCollide(mine, c.layer)) continue;
      me.c = pos;
      computeBounds(me);
      if (c.lo.x > me.hi.x || c.hi.x < me.lo.x || c.lo.y > me.hi.y || c.hi.y < me.lo.y || c.lo.z > me.hi.z || c.hi.z < me.lo.z) continue;
      Manifold m;
      if (!collide(c, me, 0.0f, &m) || !(m.sep < 0)) continue;
      pos += m.n * (-m.sep);  // m.n runs from the obstacle to the controller's box
      moved = true;
      if (m.n.y >= minNormalY) {
        if (grounded) *grounded = true;
        if (groundNormal) *groundNormal = m.n;
      }
    }
    if (!moved) break;
  }
  return pos;
}

u32 raycast(World& world, const Vector3& origin, const Vector3& dirIn, Scalar maxDistance, const QueryFilter& f, RaycastHit* out, u32 capacity) {
  if (!out || capacity == 0) return 0;
  const Scalar len = dirIn.magnitude();
  if (len < kEps) return 0;
  const Vector3 d = dirIn / len;
  const Scalar maxT = capRange(maxDistance);
  u32 n = gatherColliders(world, g_scratch.cols, kMaxColliders, nullptr);
  u32 count = 0;
  for (u32 i = 0; i < n; ++i) {
    const Col& c = g_scratch.cols[i];
    if (!layerOk(f, c) || !aabbRay(c, origin, d, maxT)) continue;
    Scalar t;
    Vector3 nrm;
    if (!rayCollider(c, origin, d, maxT, &t, &nrm)) continue;
    // Insert sorted by distance (ties keep gather order).
    u32 pos = count;
    while (pos > 0 && out[pos - 1].distance > t) --pos;
    if (pos >= capacity) continue;
    const u32 last = count < capacity ? count : capacity - 1;
    for (u32 k = last; k > pos; --k) out[k] = out[k - 1];
    out[pos].distance = t;
    out[pos].point = origin + d * t;
    out[pos].normal = nrm;
    out[pos].gameObject = c.go;
    out[pos].isTrigger = c.trigger;
    if (count < capacity) ++count;
  }
  return count;
}

bool sphereCast(World& world, const Vector3& origin, Scalar radius, const Vector3& dirIn, Scalar maxDistance, const QueryFilter& f, RaycastHit* hit) {
  const Scalar len = dirIn.magnitude();
  if (len < kEps) return false;
  const Vector3 d = dirIn / len;
  const Scalar maxT = capRange(maxDistance);
  const Vector3 end = origin + d * maxT;
  const Vector3 lo = Vector3::Min(origin, end) - Vector3(radius, radius, radius);
  const Vector3 hi = Vector3::Max(origin, end) + Vector3(radius, radius, radius);
  u32 n = gatherColliders(world, g_scratch.cols, kMaxColliders, nullptr);
  Scalar bestT = kBig;
  u32 bestI = n;
  for (u32 i = 0; i < n; ++i) {
    const Col& c = g_scratch.cols[i];
    if (!layerOk(f, c)) continue;
    if (c.lo.x > hi.x || c.hi.x < lo.x || c.lo.y > hi.y || c.hi.y < lo.y || c.lo.z > hi.z || c.hi.z < lo.z) continue;
    // f(t) = signed distance of the sphere centre minus the radius is convex along the ray: find its minimum, then the first root.
    auto fn = [&](Scalar t) { return signedDistance(c, origin + d * t, nullptr, nullptr) - radius; };
    if (fn(0) <= 0) continue;  // starts overlapping: not reported (Unity)
    Scalar a = 0, b = maxT;
    for (int k = 0; k < 24; ++k) {
      const Scalar third = (b - a) / 3.0f;
      const Scalar m1 = a + third, m2 = b - third;
      if (fn(m1) < fn(m2)) b = m2; else a = m1;
    }
    const Scalar tm = (a + b) * 0.5f;
    if (fn(tm) > 0) continue;
    Scalar r0 = 0, r1 = tm;
    for (int k = 0; k < 24; ++k) {
      const Scalar mid = (r0 + r1) * 0.5f;
      if (fn(mid) > 0) r0 = mid; else r1 = mid;
    }
    const Scalar t = r1;
    if (t < bestT) { bestT = t; bestI = i; }
  }
  if (bestI >= n) return false;
  if (hit) {
    const Col& c = g_scratch.cols[bestI];
    Vector3 cl, nm;
    const Vector3 centre = origin + d * bestT;
    signedDistance(c, centre, &cl, &nm);
    hit->distance = bestT;
    hit->point = cl;
    hit->normal = nm;
    hit->gameObject = c.go;
    hit->isTrigger = c.trigger;
  }
  return true;
}

u32 overlapSphere(World& world, const Vector3& center, Scalar radius, const QueryFilter& f, GameObject** out, u32 capacity) {
  u32 n = gatherColliders(world, g_scratch.cols, kMaxColliders, nullptr);
  struct Found { Scalar d; GameObject* go; };
  u32 count = 0;
  for (u32 i = 0; i < n; ++i) {
    const Col& c = g_scratch.cols[i];
    if (!layerOk(f, c)) continue;
    if (c.lo.x > center.x + radius || c.hi.x < center.x - radius || c.lo.y > center.y + radius || c.hi.y < center.y - radius ||
        c.lo.z > center.z + radius || c.hi.z < center.z - radius) continue;
    const Scalar sd = signedDistance(c, center, nullptr, nullptr) - radius;
    if (sd > 0) continue;
    bool dup = false;
    for (u32 k = 0; k < count; ++k) if (out && out[k] == c.go) { dup = true; break; }
    if (dup) continue;
    if (!out || capacity == 0) { ++count; continue; }
    // Insertion by distance so the nearest come first (the output array doubles as the sort key store: re-measure).
    u32 pos = count < capacity ? count : capacity;
    if (count >= capacity) {
      // Full: replace the farthest if this one is nearer.
      Scalar farthest = -kBig;
      u32 fi = 0;
      for (u32 k = 0; k < capacity; ++k) {
        GameObject* g = out[k];
        const Scalar dk = (g->transform.position() - center).sqrMagnitude();
        if (dk > farthest) { farthest = dk; fi = k; }
      }
      if ((c.go->transform.position() - center).sqrMagnitude() >= farthest) continue;
      out[fi] = c.go;
      continue;
    }
    out[pos] = c.go;
    ++count;
  }
  // Order the result by distance of the object origin (stable selection sort: tiny arrays).
  if (!out) return count;
  const u32 m = count < capacity ? count : capacity;
  {
    for (u32 i = 0; i + 1 < m; ++i) {
      u32 bi = i;
      Scalar bd = (out[i]->transform.position() - center).sqrMagnitude();
      for (u32 k = i + 1; k < m; ++k) {
        const Scalar dk = (out[k]->transform.position() - center).sqrMagnitude();
        if (dk < bd) { bd = dk; bi = k; }
      }
      if (bi != i) { GameObject* tmp = out[i]; out[i] = out[bi]; out[bi] = tmp; }
    }
  }
  return m;
}

}  // namespace physics3d
}  // namespace rensga
