// F5: view-frustum extraction (Gribb-Hartmann) and the sphere test.
#include "rensga/core/cull.h"

namespace rensga {

Frustum frustumFromViewProj(const Matrix4& vp) {
  Frustum f = {};
  const Scalar* m = vp.m;  // column-major: row i is (m[i], m[4+i], m[8+i], m[12+i])
  auto row = [&](int i, int c) { return m[c * 4 + i]; };
  for (int p = 0; p < 6; ++p) {
    const int axis = p / 2;                  // 0 x, 1 y, 2 z
    const Scalar sign = (p & 1) ? -1.0f : 1.0f;  // even: w + axis, odd: w - axis
    Scalar pl[4];
    for (int c = 0; c < 4; ++c) pl[c] = row(3, c) + sign * row(axis, c);
    Scalar len = Mathf::Sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
    if (len < 1e-12f) return Frustum();  // degenerate matrix: never cull
    for (int c = 0; c < 4; ++c) f.planes[p][c] = pl[c] / len;
  }
  f.valid = true;
  return f;
}

bool sphereInFrustum(const Frustum& f, const Vector3& c, Scalar radius) {
  if (!f.valid) return true;
  for (int p = 0; p < 6; ++p) {
    const Scalar d = f.planes[p][0] * c.x + f.planes[p][1] * c.y + f.planes[p][2] * c.z + f.planes[p][3];
    if (d < -radius) return false;
  }
  return true;
}

}  // namespace rensga
