#include "rensga/core/math.h"
#include "rensga_test.h"

using namespace rensga;

TEST(math_vector) {
  Vector3 a(1, 2, 3), b(4, 5, 6);
  CHECK_EQ(Vector3::Dot(a, b), 32.0f);
  Vector3 c = Vector3::Cross(Vector3(1, 0, 0), Vector3(0, 1, 0));
  CHECK(c == Vector3(0, 0, 1));
  CHECK_NEAR(Vector3(3, 4, 0).magnitude(), 5.0f, 1e-5f);
  CHECK_NEAR(Vector3(0, 10, 0).normalized().y, 1.0f, 1e-5f);
  CHECK(Vector3().normalized() == Vector3());
}

TEST(math_mathf) {
  CHECK_EQ(Mathf::Clamp(5.0f, 0.0f, 1.0f), 1.0f);
  CHECK_EQ(Mathf::Lerp(0.0f, 10.0f, 0.5f), 5.0f);
  CHECK_EQ(Mathf::MoveTowards(0.0f, 10.0f, 3.0f), 3.0f);
  CHECK_EQ(Mathf::MoveTowards(9.0f, 10.0f, 3.0f), 10.0f);
  CHECK_NEAR(Mathf::Repeat(-1.0f, 360.0f), 359.0f, 1e-4f);
}

TEST(math_quaternion) {
  Quaternion q = Quaternion::AngleAxis(90, Vector3(0, 1, 0));
  Vector3 v = q * Vector3(0, 0, 1);  // +Z rotated 90 deg around Y -> +X
  CHECK_NEAR(v.x, 1.0f, 1e-5f);
  CHECK_NEAR(v.z, 0.0f, 1e-5f);
  Quaternion e = Quaternion::Euler(0, 90, 0);
  CHECK_NEAR(e.y, q.y, 1e-5f);
  Vector3 back = q.conjugate() * v;
  CHECK_NEAR(back.z, 1.0f, 1e-5f);
}

TEST(math_matrix) {
  Matrix4 t = Matrix4::TRS(Vector3(1, 2, 3), Quaternion::AngleAxis(90, Vector3(0, 1, 0)), Vector3(2, 2, 2));
  Vector3 p = t.transformPoint(Vector3(0, 0, 1));  // scale 2 -> (0,0,2) -> rot -> (2,0,0) -> +t
  CHECK_NEAR(p.x, 3.0f, 1e-5f);
  CHECK_NEAR(p.y, 2.0f, 1e-5f);
  CHECK_NEAR(p.z, 3.0f, 1e-5f);
  Matrix4 id = t * t.inverse();
  for (int i = 0; i < 16; ++i) CHECK_NEAR(id.m[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-4f);
  CHECK_EQ(reinterpret_cast<uintptr_t>(&t) % 16, 0u);
  Matrix4 v = Matrix4::lookAt(Vector3(0, 0, 5), Vector3(0, 0, 0), Vector3(0, 1, 0));
  Vector3 o = v.transformPoint(Vector3(0, 0, 0));
  CHECK_NEAR(o.z, -5.0f, 1e-5f);  // in front of the camera = -Z
}
