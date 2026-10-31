// F5: stub. Agent A2 implements the frustum extraction and the sphere test (see cull.h).
#include "rensga/core/cull.h"

namespace rensga {

Frustum frustumFromViewProj(const Matrix4&) {
  Frustum f = {};
  f.valid = false;
  return f;
}
bool sphereInFrustum(const Frustum&, const Vector3&, Scalar) { return true; }

}  // namespace rensga
