// F6.08 (A2, API 6): the Unity-style Physics / Physics2D queries and the Rigidbody API.
// 3D: console-light physics in engine/core (physics3d.h): oriented boxes, spheres, capsules; queries are deterministic and write to
// caller buffers (no heap). 2D: BoxCollider2D rectangles and the solid cells of TilemapCollider2D, in virtual pixels (+Y down).
#include "rensga/Physics.h"

#include "rensga/core/physics3d.h"
#include "rensga/core/rigidbody2d.h"

namespace rensga {

namespace {

physics3d::QueryFilter filterOf(LayerMask mask, QueryTriggerInteraction t) {
  physics3d::QueryFilter f;
  f.layerMask = mask.value;
  f.triggers = t == QueryTriggerInteraction::UseGlobal ? physics3d::settings().queriesHitTriggers : t == QueryTriggerInteraction::Collide;
  return f;
}


}  // namespace

namespace Physics {

bool Raycast(const Vector3& origin, const Vector3& direction, Scalar maxDistance, LayerMask mask, QueryTriggerInteraction triggers) {
  return Raycast(origin, direction, static_cast<RaycastHit*>(nullptr), maxDistance, mask, triggers);
}
bool Raycast(const Vector3& origin, const Vector3& direction, RaycastHit* hit, Scalar maxDistance, LayerMask mask,
             QueryTriggerInteraction triggers) {
  World* w = World::active();
  if (!w) return false;
  RaycastHit h;
  if (physics3d::raycast(*w, origin, direction, maxDistance, filterOf(mask, triggers), &h, 1) == 0) return false;
  if (hit) *hit = h;
  return true;
}
bool Raycast(const Ray& ray, RaycastHit* hit, Scalar maxDistance, LayerMask mask, QueryTriggerInteraction triggers) {
  return Raycast(ray.origin, ray.direction, hit, maxDistance, mask, triggers);
}
u32 RaycastNonAlloc(const Ray& ray, RaycastHit* results, u32 capacity, Scalar maxDistance, LayerMask mask, QueryTriggerInteraction triggers) {
  World* w = World::active();
  return w ? physics3d::raycast(*w, ray.origin, ray.direction, maxDistance, filterOf(mask, triggers), results, capacity) : 0;
}
bool Linecast(const Vector3& start, const Vector3& end, RaycastHit* hit, LayerMask mask, QueryTriggerInteraction triggers) {
  const Vector3 d = end - start;
  const Scalar len = d.magnitude();
  if (len <= 0) return false;
  return Raycast(start, d, hit, len, mask, triggers);
}
bool SphereCast(const Vector3& origin, Scalar radius, const Vector3& direction, RaycastHit* hit, Scalar maxDistance, LayerMask mask,
                QueryTriggerInteraction triggers) {
  World* w = World::active();
  return w && physics3d::sphereCast(*w, origin, radius, direction, maxDistance, filterOf(mask, triggers), hit);
}
u32 OverlapSphere(const Vector3& center, Scalar radius, GameObject** results, u32 capacity, LayerMask mask, QueryTriggerInteraction triggers) {
  World* w = World::active();
  return w ? physics3d::overlapSphere(*w, center, radius, filterOf(mask, triggers), results, capacity) : 0;
}
bool CheckSphere(const Vector3& center, Scalar radius, LayerMask mask, QueryTriggerInteraction triggers) {
  World* w = World::active();
  return w && physics3d::overlapSphere(*w, center, radius, filterOf(mask, triggers), nullptr, 0) > 0;
}
Vector3 gravity() { return physics3d::settings().gravity; }
void setGravity(const Vector3& g) { physics3d::settings().gravity = g; }
void IgnoreLayerCollision(u32 a, u32 b, bool ignore) { physics3d::setIgnoreLayerCollision(a, b, ignore); }
bool GetIgnoreLayerCollision(u32 a, u32 b) { return a < 32 && b < 32 && !physics3d::layersCollide(a, b); }

}  // namespace Physics

// ---- 2D ---------------------------------------------------------------------------------------------------------------

namespace Physics2D {

namespace {

Scalar absS(Scalar v) { return v < 0 ? -v : v; }

struct Hit2 {
  Scalar t = 3.0e38f;
  Vector2 normal;
  GameObject* go = nullptr;
};

// Ray against a rectangle; the ray is outside or on the edge (a ray that starts inside is not a hit, like Unity's colliders).
bool rayRect(const Vector2& o, const Vector2& d, const Rect& r, Scalar maxT, Scalar* tOut, Vector2* n) {
  Scalar t0 = 0, t1 = maxT;
  Vector2 nrm;
  const Scalar lo[2] = {r.x, r.y}, hi[2] = {r.x + r.width, r.y + r.height}, oo[2] = {o.x, o.y}, dd[2] = {d.x, d.y};
  Scalar enterT = -3.0e38f;
  bool inside = true;
  for (int i = 0; i < 2; ++i) {
    if (absS(dd[i]) < 1e-9f) {
      if (oo[i] < lo[i] || oo[i] > hi[i]) return false;
      continue;
    }
    Scalar a = (lo[i] - oo[i]) / dd[i], b = (hi[i] - oo[i]) / dd[i];
    const bool fromLow = a < b;
    if (a > b) { const Scalar s = a; a = b; b = s; }
    if (a > enterT) {
      enterT = a;
      const Scalar sgn = fromLow ? -1.0f : 1.0f;
      nrm = i == 0 ? Vector2(sgn, 0) : Vector2(0, sgn);
    }
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
    if (t0 > t1) return false;
  }
  (void)inside;
  if (enterT < 0 || enterT > maxT) return false;
  *tOut = enterT;
  *n = nrm;
  return true;
}

bool circleRect(const Vector2& c, Scalar r, const Rect& b) {
  const Scalar qx = Mathf::Clamp(c.x, b.x, b.x + b.width), qy = Mathf::Clamp(c.y, b.y, b.y + b.height);
  const Scalar dx = c.x - qx, dy = c.y - qy;
  return dx * dx + dy * dy <= r * r;
}

struct MapGeo {
  Tilemap* tm;
  Vector2 origin;
  Scalar tw, th;
  u32 w, h;
};

template <class F>
void eachMap(World& world, LayerMask mask, F fn) {
  world.forEach<TilemapCollider2D>([&](TilemapCollider2D& tc) {
    GameObject* go = tc.gameObject();
    if (!tc.enabled() || !go || !go->activeInHierarchy() || !mask.Contains(static_cast<u32>(go->layer) & 31u)) return;
    Tilemap* tm = go->GetComponent<Tilemap>();
    const PackTilemapRec* r = tm ? tm->record() : nullptr;
    if (!r || r->tileSize == 0) return;
    const Vector3 sc = tm->transform()->lossyScale();
    const Scalar ts = static_cast<Scalar>(r->tileSize);
    MapGeo g{tm, tm->origin(), ts * absS(sc.x), ts * absS(sc.y), r->width, r->height};
    if (g.tw > 0 && g.th > 0) fn(g, go);
  });
}

// Grid traversal (Amanatides and Woo) of one tilemap. Cells outside the map are empty for queries.
bool rayMap(const MapGeo& g, const Vector2& o, const Vector2& d, Scalar maxT, Scalar* tOut, Vector2* nOut) {
  const Rect bounds(g.origin.x, g.origin.y, g.tw * static_cast<Scalar>(g.w), g.th * static_cast<Scalar>(g.h));
  Scalar t = 0;
  Vector2 enterN;
  const bool startsInside = o.x >= bounds.x && o.x < bounds.x + bounds.width && o.y >= bounds.y && o.y < bounds.y + bounds.height;
  if (!startsInside) {
    if (!rayRect(o, d, bounds, maxT, &t, &enterN)) return false;
  }
  const Vector2 p = o + d * (t + 1e-4f);
  i32 cx = static_cast<i32>(Mathf::Floor((p.x - g.origin.x) / g.tw)), cy = static_cast<i32>(Mathf::Floor((p.y - g.origin.y) / g.th));
  const i32 sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1;
  const Scalar tdx = absS(d.x) > 1e-9f ? g.tw / absS(d.x) : 3.0e38f, tdy = absS(d.y) > 1e-9f ? g.th / absS(d.y) : 3.0e38f;
  Scalar nx = absS(d.x) > 1e-9f ? ((d.x > 0 ? g.origin.x + static_cast<Scalar>(cx + 1) * g.tw : g.origin.x + static_cast<Scalar>(cx) * g.tw) - o.x) / d.x : 3.0e38f;
  Scalar ny = absS(d.y) > 1e-9f ? ((d.y > 0 ? g.origin.y + static_cast<Scalar>(cy + 1) * g.th : g.origin.y + static_cast<Scalar>(cy) * g.th) - o.y) / d.y : 3.0e38f;
  Vector2 faceN = enterN;
  const u32 limit = g.w + g.h + 4;
  for (u32 step = 0; step < limit; ++step) {
    if (cx < 0 || cy < 0 || cx >= static_cast<i32>(g.w) || cy >= static_cast<i32>(g.h)) return false;
    if (g.tm->IsSolidCell(cx, cy, false) && !(step == 0 && startsInside)) {
      if (t > maxT) return false;
      *tOut = t;
      *nOut = faceN;
      return true;
    }
    if (nx < ny) { t = nx; if (t > maxT) return false; nx += tdx; cx += sx; faceN = Vector2(static_cast<Scalar>(-sx), 0); }
    else { t = ny; if (t > maxT) return false; ny += tdy; cy += sy; faceN = Vector2(0, static_cast<Scalar>(-sy)); }
  }
  return false;
}

// Objects whose collider overlaps a circle (r > 0) or a box. `out` may be null (count only).
u32 overlap2D(bool circle, const Vector2& c, Scalar r, const Rect& box, LayerMask mask, GameObject** out, u32 capacity, bool firstOnly) {
  World* w = World::active();
  if (!w) return 0;
  u32 n = 0;
  auto add = [&](GameObject* go) {
    for (u32 i = 0; i < n && i < capacity; ++i) if (out && out[i] == go) return;
    if (out && n < capacity) out[n] = go;
    ++n;
  };
  w->forEach<BoxCollider2D>([&](BoxCollider2D& b) {
    GameObject* go = b.gameObject();
    if (firstOnly && n) return;
    if (!b.enabled() || !go || !go->activeInHierarchy() || !mask.Contains(static_cast<u32>(go->layer) & 31u)) return;
    const Rect rb = b.bounds();
    if (circle ? circleRect(c, r, rb) : rb.Overlaps(box)) add(go);
  });
  eachMap(*w, mask, [&](const MapGeo& g, GameObject* go) {
    if (firstOnly && n) return;
    const Rect q = circle ? Rect(c.x - r, c.y - r, r * 2, r * 2) : box;
    const i32 x0 = static_cast<i32>(Mathf::Floor((q.x - g.origin.x) / g.tw)), x1 = static_cast<i32>(Mathf::Floor((q.x + q.width - g.origin.x) / g.tw));
    const i32 y0 = static_cast<i32>(Mathf::Floor((q.y - g.origin.y) / g.th)), y1 = static_cast<i32>(Mathf::Floor((q.y + q.height - g.origin.y) / g.th));
    for (i32 y = y0; y <= y1; ++y)
      for (i32 x = x0; x <= x1; ++x) {
        if (x < 0 || y < 0 || x >= static_cast<i32>(g.w) || y >= static_cast<i32>(g.h) || !g.tm->IsSolidCell(x, y, false)) continue;
        const Rect cell(g.origin.x + static_cast<Scalar>(x) * g.tw, g.origin.y + static_cast<Scalar>(y) * g.th, g.tw, g.th);
        if (circle ? circleRect(c, r, cell) : cell.Overlaps(box)) { add(go); return; }
      }
  });
  return n < capacity ? n : (out ? capacity : n);
}

}  // namespace

RaycastHit2D Raycast(const Vector2& origin, const Vector2& direction, Scalar distance, LayerMask mask) {
  RaycastHit2D result;
  World* w = World::active();
  const Scalar len = direction.magnitude();
  if (!w || len < 1e-9f) return result;
  const Vector2 d = direction / len;
  const Scalar maxT = distance > 1.0e6f ? 1.0e6f : distance;
  Hit2 best;
  w->forEach<BoxCollider2D>([&](BoxCollider2D& b) {
    GameObject* go = b.gameObject();
    if (!b.enabled() || !go || !go->activeInHierarchy() || !mask.Contains(static_cast<u32>(go->layer) & 31u)) return;
    Scalar t;
    Vector2 n;
    if (rayRect(origin, d, b.bounds(), maxT, &t, &n) && t < best.t) { best.t = t; best.normal = n; best.go = go; }
  });
  eachMap(*w, mask, [&](const MapGeo& g, GameObject* go) {
    Scalar t;
    Vector2 n;
    if (rayMap(g, origin, d, maxT, &t, &n) && t < best.t) { best.t = t; best.normal = n; best.go = go; }
  });
  if (best.go) {
    result.gameObject = best.go;
    result.distance = best.t;
    result.normal = best.normal;
    result.point = origin + d * best.t;
  }
  return result;
}

GameObject* OverlapCircle(const Vector2& point, Scalar radius, LayerMask mask) {
  GameObject* g = nullptr;
  overlap2D(true, point, radius, Rect(), mask, &g, 1, true);
  return g;
}
GameObject* OverlapBox(const Vector2& center, const Vector2& size, LayerMask mask) {
  GameObject* g = nullptr;
  overlap2D(false, center, 0, Rect(center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y), mask, &g, 1, true);
  return g;
}
u32 OverlapCircleAll(const Vector2& point, Scalar radius, GameObject** results, u32 capacity, LayerMask mask) {
  return overlap2D(true, point, radius, Rect(), mask, results, capacity, false);
}
u32 OverlapBoxAll(const Vector2& center, const Vector2& size, GameObject** results, u32 capacity, LayerMask mask) {
  return overlap2D(false, center, 0, Rect(center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y), mask, results, capacity, false);
}
Scalar gravity() { return Rigidbody2DSim::gravity(); }
void setGravity(Scalar g) { Rigidbody2DSim::setGravity(g); }

}  // namespace Physics2D

// ---- Rigidbody ----------------------------------------------------------------------------------------------------------

void Rigidbody::AddForce(const Vector3& force, ForceMode mode) {
  if (isKinematic) return;  // a kinematic body is moved, never pushed
  const Scalar m = mass < 0.001f ? 0.001f : mass;
  switch (mode) {
    case ForceMode::Force: force_ += force / m; break;
    case ForceMode::Acceleration: force_ += force; break;
    case ForceMode::Impulse: impulse_ += force / m; break;
    case ForceMode::VelocityChange: impulse_ += force; break;
  }
  WakeUp();
}
void Rigidbody::setVelocity(const Vector3& v) {
  velocity_ = v;
  WakeUp();
}
void Rigidbody::MovePosition(const Vector3& position) {
  moveTarget_ = position;
  hasMove_ = true;
  WakeUp();
}
void Rigidbody::Sleep() {
  sleeping_ = true;
  velocity_ = Vector3();
  force_ = Vector3();
  impulse_ = Vector3();
}
void Rigidbody::WakeUp() {
  sleeping_ = false;
  sleepTicks_ = 0;
}

}  // namespace rensga
