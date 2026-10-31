#include "rensga/core/rigidbody2d.h"

#include "rensga/core/world.h"

namespace rensga {

// F4 contract: the hook exists and is called from Gameplay2D::fixedStep; the behaviour is implemented later.
void Rigidbody2DSim::fixedStep(World& world) { (void)world; }

}  // namespace rensga
