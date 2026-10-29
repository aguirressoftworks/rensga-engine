#include <string.h>
#include <fstream>
#include <sstream>

#include "rensga/cooker/cooker.h"
#include "rensga_test.h"

using namespace rensga;

static PrimitiveParams load(const char* name) {
  std::ifstream in(std::string(RENSGA_TEMPLATE_DIR) + "/Assets/Primitives/" + name + ".prim.json");
  std::stringstream ss;
  ss << in.rdbuf();
  PrimitiveParams p;
  std::string err;
  CHECK(parsePrimitive(ss.str(), p, err));
  return p;
}

static void bounds(const PrimitiveMesh& m, Vector3& lo, Vector3& hi) {
  lo = hi = m.positions[0];
  for (const Vector3& v : m.positions) {
    lo = Vector3(v.x < lo.x ? v.x : lo.x, v.y < lo.y ? v.y : lo.y, v.z < lo.z ? v.z : lo.z);
    hi = Vector3(v.x > hi.x ? v.x : hi.x, v.y > hi.y ? v.y : hi.y, v.z > hi.z ? v.z : hi.z);
  }
}

TEST(primitives_all_shapes_wind_outward_and_match_colliders) {
  ConsoleTargetData t;
  const char* names[] = {"cube", "sphere", "capsule", "plane", "ramp"};
  for (const char* n : names) {
    PrimitiveParams p = load(n);
    PrimitiveMesh m = generatePrimitive(p, t);
    CHECK(m.triangleCount() > 0);
    CHECK_EQ(m.positions.size(), m.normals.size());
    for (size_t i = 0; i + 2 < m.positions.size(); i += 3) {
      Vector3 a = m.positions[i], b = m.positions[i + 1], c = m.positions[i + 2];
      Vector3 n2 = Vector3::Cross(b - a, c - a).normalized();
      CHECK(Vector3::Dot(n2, m.normals[i]) > 0.99f);  // winding agrees with the stored normal
    }
    Vector3 lo, hi;
    bounds(m, lo, hi);
    PrimitiveCollider col = colliderForPrimitive(p);
    if (col.isSphere) {
      CHECK_NEAR(hi.x - lo.x, 2 * col.radius, 1e-3f);
    } else if (strcmp(n, "capsule") == 0 || strcmp(n, "cube") == 0 || strcmp(n, "ramp") == 0) {
      CHECK_NEAR(hi.y - lo.y, col.size.y, 1e-3f);
      CHECK_NEAR(hi.x - lo.x, col.size.x, 1e-3f);
    }
  }
  // Closed convex solids: normals point away from the origin.
  for (const char* n : {"cube", "sphere", "capsule", "ramp"}) {
    PrimitiveMesh m = generatePrimitive(load(n), t);
    Vector3 inside = strcmp(n, "ramp") == 0 ? Vector3(0, -0.25f, 0) : Vector3();
    for (size_t i = 0; i + 2 < m.positions.size(); i += 3) {
      Vector3 mid = (m.positions[i] + m.positions[i + 1] + m.positions[i + 2]) / 3.0f - inside;
      CHECK(Vector3::Dot(m.normals[i], mid) > 0);
    }
  }
}

TEST(primitives_tessellation_from_console_target) {
  ConsoleTargetData lo, hi;
  lo.sphereSegments = 8;
  hi.sphereSegments = 16;
  PrimitiveParams p = load("sphere");
  CHECK(generatePrimitive(p, hi).triangleCount() > generatePrimitive(p, lo).triangleCount());
}
