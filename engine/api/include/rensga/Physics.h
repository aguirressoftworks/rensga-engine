// MVP-30 slot: minimal physics. The physics step is hooked into GameLoop::setFixedStepHook.
#pragma once
#include "rensga/core/world.h"

namespace rensga {
namespace physics {

// Moves CharacterControllers (gravity + AABB collision against static BoxColliders) and sends
// OnTriggerEnter for SphereCollider/BoxCollider triggers. Pass to GameLoop::setFixedStepHook.
void fixedStep(World& world, Scalar fixedDelta, void* user);

}  // namespace physics
}  // namespace rensga
