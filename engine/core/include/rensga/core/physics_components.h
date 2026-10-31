// F6 contract (C0): 3D physics components. The fields are final; the behaviour is stubbed until A2 (Stage 1, API 6):
// a scene that holds a Rigidbody or a CapsuleCollider loads, cooks and round-trips, and nothing moves or collides because of
// them. The methods are declared here and defined (as stubs returning safe defaults) in engine/api/src/physics_api.cpp.
// Console-light rules (user decision 2026-10-31): oriented boxes, spheres, capsules; gravity, velocity, AddForce and an impulse
// response with NO rotational dynamics; deterministic, in Scalar.
#pragma once
#include "rensga/core/behaviour_events.h"
#include "rensga/core/object.h"

namespace rensga {

// Rigidbody.constraints bit mask (Unity's RigidbodyConstraints).
namespace RigidbodyConstraints {
constexpr i32 None = 0, FreezePositionX = 1, FreezePositionY = 2, FreezePositionZ = 4, FreezeRotationX = 8, FreezeRotationY = 16,
              FreezeRotationZ = 32, FreezePosition = 7, FreezeRotation = 56, FreezeAll = 63;
}

struct Rigidbody : Component {
  REFLECT(Rigidbody)
  RANGE(0.001f, 1000)
  SERIALIZE_FIELD(Scalar, mass, 1.0f)
  RANGE(0, 100)
  SERIALIZE_FIELD(Scalar, drag, 0.0f)
  SERIALIZE_FIELD(bool, useGravity, true)
  TOOLTIP("A kinematic body is moved by MovePosition or the Transform, never by forces; it pushes dynamic bodies.")
  SERIALIZE_FIELD(bool, isKinematic, false)
  TOOLTIP("Bit mask: 1/2/4 freeze position X/Y/Z, 8/16/32 freeze rotation X/Y/Z (RigidbodyConstraints).")
  RANGE(0, 63)
  SERIALIZE_FIELD(i32, constraints, 0)
  TOOLTIP("Only None is simulated in F6; Interpolate is stored for later.")
  SERIALIZE_ENUM(interpolation, 0, "None|Interpolate")

  // Runtime state (not reflected), owned by the physics step (A2).
  Vector3 velocity_;
  Vector3 force_;    // accumulated this step (Force / Acceleration)
  Vector3 impulse_;  // accumulated this step (Impulse / VelocityChange), applied as a velocity change
  bool sleeping_ = false;

  // ---- Unity API. Defined in engine/api/src/physics_api.cpp (stubs until A2).
  void AddForce(const Vector3& force, ForceMode mode = ForceMode::Force);
  Vector3 velocity() const { return velocity_; }
  void setVelocity(const Vector3& v);
  // Moves a kinematic body to `position` (world space) at the next fixed step, pushing dynamic bodies on the way.
  void MovePosition(const Vector3& position);
  void Sleep();
  void WakeUp();
  bool IsSleeping() const { return sleeping_; }
};

// A capsule along one local axis. Center and size are in the object's local space and scale with the lossy scale.
struct CapsuleCollider : Component {
  REFLECT(CapsuleCollider)
  SERIALIZE_FIELD(Vector3, center, Vector3(0, 0, 0))
  SERIALIZE_FIELD(Scalar, radius, 0.5f)
  SERIALIZE_FIELD(Scalar, height, 2.0f)
  TOOLTIP("The capsule's long axis in local space.")
  SERIALIZE_ENUM(direction, 1, "X-Axis|Y-Axis|Z-Axis")
  SERIALIZE_FIELD(bool, isTrigger, false)
};

}  // namespace rensga
