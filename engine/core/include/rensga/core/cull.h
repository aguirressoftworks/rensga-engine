// F5: view-frustum culling for renderWorld (sphere vs frustum). Implemented in engine/core/src/cull.cpp.
#pragma once
#include "rensga/core/math.h"

namespace rensga {

struct Frustum {
  Scalar planes[6][4];  // a*x + b*y + c*z + d >= 0 is inside; left, right, bottom, top, near, far; normals unit length
  bool valid = false;   // false = never cull
};

// Extracts the six planes from view * projection (Gribb-Hartmann on the column-major Matrix4, OpenGL clip space).
Frustum frustumFromViewProj(const Matrix4& viewProj);
// True when the sphere touches the frustum. An invalid frustum always returns true.
bool sphereInFrustum(const Frustum& f, const Vector3& center, Scalar radius);

}  // namespace rensga
