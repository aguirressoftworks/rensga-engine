// MVP-30 slot: minimal physics. The physics step is hooked into GameLoop::setFixedStepHook.
//
// F6 contract (C0): the Unity-style query API below is final; every function is a stub returning a safe default (no hit, 0 results)
// until A2 (Stage 1, API 6) implements it in engine/api/src/physics_api.cpp. Layers are GameObject::layer (low 5 bits, 32 layers).
// Console-light: oriented boxes, spheres and capsules; queries are deterministic and allocation free (results go to a caller
// buffer). The same signatures serve every profile; nothing here is gated by a ConsoleTarget feature (documented in
// docs/rensga-engine/api-availability.md: no new feature flags in F6).
#pragma once
#include "rensga/core/behaviour_events.h"
#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/physics_components.h"
#include "rensga/core/world.h"

namespace rensga {
namespace physics {

// Moves CharacterControllers (gravity + AABB collision against static BoxColliders) and sends
// OnTriggerEnter for SphereCollider/BoxCollider triggers. Pass to GameLoop::setFixedStepHook.
void fixedStep(World& world, Scalar fixedDelta, void* user);
// Forgets which trigger pairs overlapped (call when starting a fresh world).
void resetTriggerMemory();

}  // namespace physics

// ---- 3D queries (Unity: Physics.*) ---------------------------------------------------------------------------------------
namespace Physics {
constexpr Scalar kInfinity = Mathf::Infinity;
constexpr LayerMask kDefaultRaycastLayers = LayerMask(0xFFFFFFFFu);

bool Raycast(const Vector3& origin, const Vector3& direction, Scalar maxDistance = kInfinity, LayerMask mask = kDefaultRaycastLayers,
             QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);
// `hit` may be null. On a miss it is left untouched.
bool Raycast(const Vector3& origin, const Vector3& direction, RaycastHit* hit, Scalar maxDistance = kInfinity,
             LayerMask mask = kDefaultRaycastLayers, QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);
bool Raycast(const Ray& ray, RaycastHit* hit, Scalar maxDistance = kInfinity, LayerMask mask = kDefaultRaycastLayers,
             QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);
// Every hit along the ray, nearest first, into `results` (at most `capacity`); returns the number written.
u32 RaycastNonAlloc(const Ray& ray, RaycastHit* results, u32 capacity, Scalar maxDistance = kInfinity,
                    LayerMask mask = kDefaultRaycastLayers, QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);
bool Linecast(const Vector3& start, const Vector3& end, RaycastHit* hit = nullptr, LayerMask mask = kDefaultRaycastLayers,
              QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);
bool SphereCast(const Vector3& origin, Scalar radius, const Vector3& direction, RaycastHit* hit = nullptr,
                Scalar maxDistance = kInfinity, LayerMask mask = kDefaultRaycastLayers,
                QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);
// Objects whose collider overlaps the sphere, into `results` (at most `capacity`, nearest first); returns the number written.
u32 OverlapSphere(const Vector3& center, Scalar radius, GameObject** results, u32 capacity, LayerMask mask = kDefaultRaycastLayers,
                  QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);
bool CheckSphere(const Vector3& center, Scalar radius, LayerMask mask = kDefaultRaycastLayers,
                 QueryTriggerInteraction triggers = QueryTriggerInteraction::UseGlobal);

// World gravity (PHY3 / ProjectSettings/Physics.json, default (0, -9.81, 0)) and the 3D layer collision matrix.
Vector3 gravity();
void setGravity(const Vector3& g);
void IgnoreLayerCollision(u32 layerA, u32 layerB, bool ignore = true);
bool GetIgnoreLayerCollision(u32 layerA, u32 layerB);
}  // namespace Physics

// ---- 2D queries (Unity: Physics2D.*), virtual pixels, +Y down. The namespace also holds IgnoreLayerCollision (gameplay.h). ----
namespace Physics2D {
constexpr Scalar kInfinity = Mathf::Infinity;
constexpr LayerMask kDefaultLayers = LayerMask(0xFFFFFFFFu);

RaycastHit2D Raycast(const Vector2& origin, const Vector2& direction, Scalar distance = kInfinity, LayerMask mask = kDefaultLayers);
// First object with a collider (BoxCollider2D or a solid tile of a TilemapCollider2D) overlapping the circle / box, null if none.
GameObject* OverlapCircle(const Vector2& point, Scalar radius, LayerMask mask = kDefaultLayers);
GameObject* OverlapBox(const Vector2& center, const Vector2& size, LayerMask mask = kDefaultLayers);
u32 OverlapCircleAll(const Vector2& point, Scalar radius, GameObject** results, u32 capacity, LayerMask mask = kDefaultLayers);
u32 OverlapBoxAll(const Vector2& center, const Vector2& size, GameObject** results, u32 capacity, LayerMask mask = kDefaultLayers);
// Gravity of Rigidbody2D in px/s^2 (default 1400, see Rigidbody2DSim::setGravity).
Scalar gravity();
void setGravity(Scalar pxPerSecondSquared);
}  // namespace Physics2D
}  // namespace rensga
