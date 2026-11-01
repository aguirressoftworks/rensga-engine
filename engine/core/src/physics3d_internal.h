// Internal to physics3d_shapes.cpp / physics3d_step.cpp.
#pragma once
#include "rensga/core/physics3d.h"

namespace rensga {
namespace physics3d {

enum class Shape : u8 { Sphere = 0, Capsule = 1, Box = 2 };  // order matters: collide() takes the lower rank as A

// A collider in world space, rebuilt from the components each step / query.
struct Col {
  Shape type = Shape::Box;
  bool trigger = false;
  bool proxy = false;    // a CharacterController, seen as an axis-aligned box
  bool legacyTrigger = false;  // (proxy vs a Box/Sphere trigger: Enter/Exit stay with physics.cpp)
  u8 layer = 0;
  i32 body = -1;
  GameObject* go = nullptr;
  u32 goIndex = 0, goGen = 0;
  Vector3 c;            // world center (box, sphere, capsule midpoint)
  Vector3 off;          // c minus the body position at gather time
  Vector3 ax[3];        // box axes (unit)
  Vector3 h;            // box half extents
  Scalar r = 0;         // sphere / capsule radius
  Vector3 dir;          // capsule axis (unit)
  Scalar hl = 0;        // capsule half segment length (without the caps)
  Vector3 lo, hi;       // world AABB
};

struct Manifold {
  Vector3 n;  // from A to B
  Vector3 p;  // contact point
  Scalar sep = 0;  // distance between the surfaces, negative = penetration depth
};

constexpr Scalar kMargin = 0.02f;   // contacts are kept while the surfaces are closer than this (speculative contacts)

u32 gatherColliders(World& world, Col* out, u32 capacity, u32* dropped);
void computeBounds(Col& c);
// A.type must rank <= B.type. True when sep < margin.
bool collide(const Col& a, const Col& b, Scalar margin, Manifold* m);
// Signed distance from p to the collider surface (negative inside), with the closest surface point and the outward normal there.
Scalar signedDistance(const Col& c, const Vector3& p, Vector3* closest, Vector3* normal);
bool layerOk(const QueryFilter& f, const Col& c);

}  // namespace physics3d
}  // namespace rensga
