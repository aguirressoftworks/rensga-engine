// Built-in components. Registered through reflection like any script.
// Physics (CharacterController::Move, trigger detection) lives in engine/api/src/physics.cpp.
#pragma once
#include "rensga/core/object.h"

namespace rensga {

// Forces this translation unit's registrars to be linked and runs them. Called by World::init.
void registerBuiltinComponents();

struct MeshRenderer : Component {
  REFLECT(MeshRenderer)
  SERIALIZE_FIELD(Guid, mesh, Guid())
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
  // Runtime only (not reflected): index into the pack MESH table, set by the pack loader.
  u32 meshIndex = kInvalidIndex;
  void OnCloned(const Component* source) override { meshIndex = static_cast<const MeshRenderer*>(source)->meshIndex; }
};

struct BoxCollider : Component {
  REFLECT(BoxCollider)
  SERIALIZE_FIELD(Vector3, center, Vector3(0, 0, 0))
  SERIALIZE_FIELD(Vector3, size, Vector3(1, 1, 1))
  SERIALIZE_FIELD(bool, isTrigger, false)
};

struct SphereCollider : Component {
  REFLECT(SphereCollider)
  SERIALIZE_FIELD(Scalar, radius, 0.5f)
  SERIALIZE_FIELD(bool, isTrigger, false)
};

struct CharacterController : Component {
  REFLECT(CharacterController)
  SERIALIZE_FIELD(Scalar, height, 2.0f)
  SERIALIZE_FIELD(Scalar, radius, 0.5f)
  SERIALIZE_FIELD(Scalar, gravity, -20.0f)
  SERIALIZE_FIELD(Scalar, stepOffset, 0.3f)
  // Runtime state (not reflected).
  Vector3 velocity;
  bool grounded = false;
  // Moves by `motion` (world space) with gravity and AABB collision. Implemented in engine/api.
  void Move(const Vector3& motion);
  bool isGrounded() const { return grounded; }
};

struct Camera : Component {
  REFLECT(Camera)
  SERIALIZE_FIELD(Scalar, fov, 60.0f)
  SERIALIZE_FIELD(Scalar, nearClip, 0.3f)
  SERIALIZE_FIELD(Scalar, farClip, 200.0f)
};

// The cooker bakes this light into vertex colors; the runtime does not light at all.
struct DirectionalLight : Component {
  REFLECT(DirectionalLight)
  SERIALIZE_FIELD(Vector3, direction, Vector3(-0.4f, -1.0f, -0.3f))
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
};

}  // namespace rensga
