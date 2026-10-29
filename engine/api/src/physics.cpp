// Foundation stub: the engine role replaces this with real AABB collision and triggers.
#include "rensga/Physics.h"

#include "rensga/core/components.h"

namespace rensga {

void CharacterController::Move(const Vector3& motion) {
  transform()->Translate(motion);  // no collision yet
}

namespace physics {
void fixedStep(World&, Scalar, void*) {}
}  // namespace physics

}  // namespace rensga
