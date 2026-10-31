// F6 contract stubs (C0) for API 6 physics. Every query returns "nothing hit" and every Rigidbody call is a no-op (the velocity is
// remembered so a script can read back what it set) until A2 implements them. The signatures are final (rensga/Physics.h,
// rensga/core/physics_components.h).
#include "rensga/Physics.h"

namespace rensga {

namespace {
Vector3 g_gravity3(0, -9.81f, 0);
Scalar g_gravity2 = 1400.0f;
}  // namespace

namespace Physics {

// TODO(A2 physics): static + dynamic collider world, oriented boxes / spheres / capsules, layer mask and PHY3 matrix.
bool Raycast(const Vector3&, const Vector3&, Scalar, LayerMask, QueryTriggerInteraction) { return false; }
bool Raycast(const Vector3&, const Vector3&, RaycastHit*, Scalar, LayerMask, QueryTriggerInteraction) { return false; }
bool Raycast(const Ray&, RaycastHit*, Scalar, LayerMask, QueryTriggerInteraction) { return false; }
u32 RaycastNonAlloc(const Ray&, RaycastHit*, u32, Scalar, LayerMask, QueryTriggerInteraction) { return 0; }
bool Linecast(const Vector3&, const Vector3&, RaycastHit*, LayerMask, QueryTriggerInteraction) { return false; }
bool SphereCast(const Vector3&, Scalar, const Vector3&, RaycastHit*, Scalar, LayerMask, QueryTriggerInteraction) { return false; }
u32 OverlapSphere(const Vector3&, Scalar, GameObject**, u32, LayerMask, QueryTriggerInteraction) { return 0; }
bool CheckSphere(const Vector3&, Scalar, LayerMask, QueryTriggerInteraction) { return false; }
// TODO(A2 physics): gravity and the matrix come from PHY3 (loadFromPack) and ProjectSettings/Physics.json.
Vector3 gravity() { return g_gravity3; }
void setGravity(const Vector3& g) { g_gravity3 = g; }
void IgnoreLayerCollision(u32, u32, bool) {}
bool GetIgnoreLayerCollision(u32, u32) { return false; }

}  // namespace Physics

namespace Physics2D {

// TODO(A2 physics): 2D queries over BoxCollider2D and TilemapCollider2D (virtual pixels).
RaycastHit2D Raycast(const Vector2&, const Vector2&, Scalar, LayerMask) { return RaycastHit2D(); }
GameObject* OverlapCircle(const Vector2&, Scalar, LayerMask) { return nullptr; }
GameObject* OverlapBox(const Vector2&, const Vector2&, LayerMask) { return nullptr; }
u32 OverlapCircleAll(const Vector2&, Scalar, GameObject**, u32, LayerMask) { return 0; }
u32 OverlapBoxAll(const Vector2&, const Vector2&, GameObject**, u32, LayerMask) { return 0; }
Scalar gravity() { return g_gravity2; }
void setGravity(Scalar g) { g_gravity2 = g; }

}  // namespace Physics2D

// TODO(A2 physics): Rigidbody integration (gravity, drag, constraints), impulse response, kinematic MovePosition.
void Rigidbody::AddForce(const Vector3&, ForceMode) {}
void Rigidbody::setVelocity(const Vector3& v) { velocity_ = v; }
void Rigidbody::MovePosition(const Vector3&) {}
void Rigidbody::Sleep() { sleeping_ = true; }
void Rigidbody::WakeUp() { sleeping_ = false; }

}  // namespace rensga
