// F6.08 (A2 physics): console-light 3D physics. Oriented boxes, spheres and capsules (BoxCollider, SphereCollider, CapsuleCollider,
// plus a CharacterController as a box proxy), Rigidbody bodies with gravity, drag, AddForce / MovePosition / constraints and a
// sequential-impulse contact response WITHOUT rotational dynamics (bodies never rotate), Enter / Stay / Exit callbacks dispatched after
// the step, and the ray / sphere / overlap queries behind rensga/Physics.h.
//
// Deterministic by construction: Scalar only (add, mul, div, sqrt: no libm in the simulation), everything ordered by component pool
// slot, a fixed iteration count, fixed-capacity static tables (no heap per frame). Absent Rigidbody and CharacterController, step()
// does nothing, so scenes made before F6 are unchanged.
//
// Limits (documented in docs/rensga-engine/physics.md): kMaxColliders colliders and kMaxBodies bodies are simulated per step (the rest
// are ignored and counted in Stats::dropped), objects with an index >= kMaxTrackedObjects take no part in events, no continuous collision
// detection (the broadphase is swept, the narrowphase is discrete), one contact point per collider pair.
#pragma once
#include "rensga/core/behaviour_events.h"
#include "rensga/core/components.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/world.h"

namespace rensga {
namespace physics3d {

constexpr u32 kMaxColliders = 256;
constexpr u32 kMaxBodies = 128;
constexpr u32 kMaxContacts = 512;
constexpr u32 kMaxTrackedObjects = 2048;
constexpr u32 kSolverIterations = 8;
constexpr u32 kPositionIterations = 4;
constexpr u32 kSleepSteps = 30;

// Global settings (PHY3 / ProjectSettings/Physics.json). masks[a] bit b set = layers a and b collide.
struct Settings {
  Vector3 gravity = Vector3(0, -9.81f, 0);
  bool queriesHitTriggers = true;
  u32 masks[32];
  Scalar bounceThreshold = 2.0f;   // impact speed below which there is no bounce (m/s)
  Scalar sleepThreshold = 0.05f;   // speed under which a body counts as at rest (m/s)
  Scalar friction = 0.4f;          // Coulomb friction of every contact (no PhysicMaterial yet)
  Scalar bounciness = 0.0f;        // restitution of every contact
  Settings() { for (u32& m : masks) m = 0xFFFFFFFFu; }
};
Settings& settings();
void resetSettings();                       // defaults: gravity (0,-9.81,0), every layer collides, queries hit triggers
void loadFromPack(const PackView* pack);    // PHY3 if present, else resetSettings
bool layersCollide(u32 layerA, u32 layerB);
void setIgnoreLayerCollision(u32 a, u32 b, bool ignore);

struct Stats {
  u32 colliders = 0, bodies = 0, pairs = 0, contacts = 0, dropped = 0;
  u32 enters = 0, stays = 0, exits = 0;
};
const Stats& stats();

// Forgets the contact pairs and the kinematic history (call when a fresh world starts). Does not touch the settings.
void resetState();
// One fixed step: forces, contacts, positions, then Enter / Stay / Exit callbacks. `dt` is the fixed delta.
void step(World& world, Scalar dt);

// CharacterController support: pushes the box (center `pos`, half extents `half`) out of the solid colliders that the controller's own
// axis-aligned sweep does not handle: oriented (rotated) boxes, spheres, capsules and every collider that belongs to a Rigidbody.
// `grounded` is set when a contact normal points up at least `minNormalY` (cos of the slope limit). Returns the corrected center;
// scenes with only axis-aligned static boxes get `pos` back bit for bit.
Vector3 resolveBox(World& world, const GameObject* self, const Vector3& pos, const Vector3& half, Scalar minNormalY, bool* grounded,
                   Vector3* groundNormal);

// True for the boxes CharacterController::Move still sweeps as axis-aligned boxes (the pre-F6 behaviour): not rotated and not driven by a
// Rigidbody. Every other solid collider goes through resolveBox.
bool isLegacyAabb(World& world, const BoxCollider& box);

// ---- queries (rensga/Physics.h wraps these) -----------------------------------------------------------------------------------
struct QueryFilter {
  u32 layerMask = 0xFFFFFFFFu;
  bool triggers = true;  // report trigger colliders
};
// Every collider hit along the ray, nearest first (at most `capacity`); returns the count. Colliders that contain the origin are not hit.
u32 raycast(World& world, const Vector3& origin, const Vector3& dir, Scalar maxDistance, const QueryFilter& f, RaycastHit* out, u32 capacity);
bool sphereCast(World& world, const Vector3& origin, Scalar radius, const Vector3& dir, Scalar maxDistance, const QueryFilter& f, RaycastHit* hit);
// Objects whose colliders overlap the sphere, nearest first (a CharacterController counts as its box).
u32 overlapSphere(World& world, const Vector3& center, Scalar radius, const QueryFilter& f, GameObject** out, u32 capacity);

}  // namespace physics3d
}  // namespace rensga
