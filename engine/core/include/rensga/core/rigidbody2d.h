// F4 contract (frozen): Rigidbody2D, the 2D body that BoxCollider2D::Move drives from the fixed step.
//
// Behaviour lands in F4.08 part 1 (agent "2d-core"). Today the component is only declared and the step does nothing.
// Contract for that implementation:
//   * Gameplay2D::fixedStep calls Rigidbody2DSim::fixedStep(world) FIRST (before trigger pairs are computed), once per
//     fixed update, so a body moved this step is seen by the trigger events of the same step.
//   * Dynamic bodies integrate gravity (Physics gravity scaled by gravityScale) into velocity and move by
//     BoxCollider2D::Move on the same object; a hit zeroes the velocity component on that axis.
//   * Kinematic bodies move by their velocity and ignore gravity; Static bodies never move.
//   * Everything is in virtual pixels and seconds, +Y down, in Scalar (fixed-step determinism, RULES 1.9).
//   * A body needs a BoxCollider2D on the same object; without one the step skips it.
#pragma once
#include "rensga/core/gameplay.h"

namespace rensga {

// Values of Rigidbody2D::bodyType (the reflection layer has no enum field type, so it is an I32).
namespace RigidbodyType {
constexpr i32 Dynamic = 0;    // gravity and collisions
constexpr i32 Kinematic = 1;  // moved by velocity only, collides with tiles
constexpr i32 Static = 2;     // never moves
}  // namespace RigidbodyType

struct Rigidbody2D : Component {
  REFLECT(Rigidbody2D)
  REQUIRE_COMPONENT(BoxCollider2D)
  TOOLTIP("0 = Dynamic (gravity and collisions), 1 = Kinematic (moves by velocity only), 2 = Static (never moves).")
  RANGE(0, 2)
  SERIALIZE_FIELD(i32, bodyType, 0)
  TOOLTIP("Multiplier of the world gravity for this body.")
  SERIALIZE_FIELD(Scalar, gravityScale, 1.0f)
  TOOLTIP("Velocity in virtual pixels per second (+Y is down).")
  SERIALIZE_FIELD(Scalar, velocityX, 0.0f)
  SERIALIZE_FIELD(Scalar, velocityY, 0.0f)
};

namespace Rigidbody2DSim {
// Steps every enabled Rigidbody2D by Time::fixedDeltaTime(). Called by Gameplay2D::fixedStep (see the contract above).
//
// Per body, in pool order, with dt = Time::fixedDeltaTime():
//   Dynamic:   velocityY += gravity() * gravityScale * dt; Move(velocityX * dt, velocityY * dt) on the BoxCollider2D of
//              the same object; then hitDown (or a grounded body that is not moving up) / hitUp zero velocityY, and
//              hitLeft / hitRight zero velocityX. A body resting on the ground stays grounded: gravity is added again
//              each step and removed again by the landing.
//   Kinematic: Move(velocityX * dt, velocityY * dt); no gravity; the velocity is never changed (a script may read
//              BoxCollider2D::grounded or flip the velocity itself).
//   Static:    nothing.
// Bodies that are disabled, inactive in the hierarchy, or have no (enabled) BoxCollider2D are skipped. Trigger
// colliders move but never block. The auto drop-through of BoxCollider2D (Down + South on the pad) is switched off
// while a body is stepped: scripts call BoxCollider2D::DropThrough() themselves. No heap allocation.
void fixedStep(World& world);

// World gravity in virtual pixels per second squared (+Y is down), shared by every Dynamic body (times its gravityScale).
// The default is 1400, the value the platformer sample uses. There is no per-project setting yet (PH2D carries only the
// layer matrix); a script or runtime main may call setGravity(). Not reset by scene loads.
constexpr Scalar kDefaultGravity = 1400.0f;
Scalar gravity();
void setGravity(Scalar pixelsPerSecondSquared);
}  // namespace Rigidbody2DSim

}  // namespace rensga
