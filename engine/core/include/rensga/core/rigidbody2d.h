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
void fixedStep(World& world);
}  // namespace Rigidbody2DSim

}  // namespace rensga
