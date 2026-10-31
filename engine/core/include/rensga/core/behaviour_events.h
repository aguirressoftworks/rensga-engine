// F6 contract (C0): data types of the Behaviour callbacks and of the physics queries. Plain data, no pointers that outlive
// the callback, no heap. Included by object.h so Behaviour can declare the callbacks; the query functions are in
// rensga/Physics.h (engine/api).
//
// Ownership of the behaviour behind these types (Stage 1): A2 physics fills Collision / RaycastHit, B1 animation fills
// AnimationEvent.
#pragma once
#include "rensga/core/math.h"
#include "rensga/core/types.h"

namespace rensga {

struct GameObject;

// ---- 3D physics ------------------------------------------------------------------------------------------------------

// Unity's ForceMode for Rigidbody::AddForce.
enum class ForceMode : u8 { Force, Acceleration, Impulse, VelocityChange };
// Physics queries: whether trigger colliders are hit. UseGlobal = the Physics setting (PHY3 flag kPhy3QueriesHitTriggers).
enum class QueryTriggerInteraction : u8 { UseGlobal, Ignore, Collide };

// A set of layers (bit n = layer n of GameObject::layer, 32 layers). Implicitly a u32 so `1u << layer` works.
struct LayerMask {
  u32 value = 0xFFFFFFFFu;
  constexpr LayerMask() {}
  constexpr LayerMask(u32 bits) : value(bits) {}
  constexpr operator u32() const { return value; }
  static constexpr LayerMask All() { return LayerMask(0xFFFFFFFFu); }
  static constexpr LayerMask None() { return LayerMask(0u); }
  static constexpr LayerMask Layer(u32 layer) { return LayerMask(layer < 32 ? (1u << layer) : 0u); }
  constexpr bool Contains(u32 layer) const { return layer < 32 && ((value >> layer) & 1u); }
};

struct Ray {
  Vector3 origin;
  Vector3 direction = Vector3(0, 0, 1);
  Ray() {}
  Ray(const Vector3& o, const Vector3& d) : origin(o), direction(d) {}
  Vector3 GetPoint(Scalar distance) const { return origin + direction * distance; }
};

struct RaycastHit {
  Vector3 point;
  Vector3 normal;
  Scalar distance = 0;
  GameObject* gameObject = nullptr;  // the object whose collider was hit (transient: use inside the calling frame)
  bool isTrigger = false;
};

// 2D queries work in virtual pixels like the rest of the 2D module (+Y down). No hit = gameObject null.
struct RaycastHit2D {
  Vector2 point;
  Vector2 normal;
  Scalar distance = 0;
  GameObject* gameObject = nullptr;
  bool hit() const { return gameObject != nullptr; }
};

struct ContactPoint {
  Vector3 point;
  Vector3 normal;  // points from the other collider toward this one
};
constexpr u32 kMaxContactPoints = 4;

// Passed to Behaviour::OnCollisionEnter / Stay / Exit after the physics step. `gameObject` is the other object.
struct Collision {
  GameObject* gameObject = nullptr;
  Vector3 relativeVelocity;
  Vector3 impulse;
  ContactPoint contacts[kMaxContactPoints];
  u32 contactCount = 0;
};

// Passed to the Collision2D overloads of OnCollisionEnter2D / Stay2D / Exit2D (virtual pixels).
struct Collision2D {
  GameObject* gameObject = nullptr;
  Vector2 relativeVelocity;
  Vector2 point;
  Vector2 normal;
};

// ---- animation -----------------------------------------------------------------------------------------------------------

// An event fired by a property clip (PCLP event) while an Animator or Animation plays. Dispatched to every Behaviour on the
// object through Behaviour::OnAnimationEvent, and to the method named `functionName` through the METHOD registry when the
// method takes no argument or one of Scalar, i32 (intParameter) (B1 decides the exact rules; see AnimationEvents in
// rensga/core/animator.h). The strings are valid only during the call.
struct AnimationEvent {
  const char* functionName = "";
  i32 intParameter = 0;
  Scalar floatParameter = 0;
  const char* stringParameter = "";
  Scalar time = 0;  // clip time in seconds when the event fired
};

}  // namespace rensga
