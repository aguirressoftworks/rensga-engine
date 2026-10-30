// API 1 (F2.06): Unity golden values and determinism checks for the math types.
// Golden numbers were computed in double precision from Unity's documented formulas.
#include <math.h>

#include "rensga/core/math.h"
#include "rensga_test.h"

using namespace rensga;

TEST(mathu_trig_accuracy) {
  double worstSin = 0, worstCos = 0, worstAtan = 0, worstAsin = 0, worstTan = 0;
  for (int i = -4000; i <= 4000; ++i) {
    float x = i * (3.14159265f / 4000.0f);
    double es = fabs((double)Mathf::Sin(x) - sin((double)x));
    double ec = fabs((double)Mathf::Cos(x) - cos((double)x));
    if (es > worstSin) worstSin = es;
    if (ec > worstCos) worstCos = ec;
    float t = i * 0.01f;
    double ea = fabs((double)Mathf::Atan(t) - atan((double)t));
    if (ea > worstAtan) worstAtan = ea;
    if (fabs(x) < 1.4f) {
      double et = fabs((double)Mathf::Tan(x) - tan((double)x)) / (1.0 + tan((double)x) * tan((double)x));
      if (et > worstTan) worstTan = et;
    }
    float u = i / 4000.0f;
    double eas = fabs((double)Mathf::Asin(u) - asin((double)u));
    double eac = fabs((double)Mathf::Acos(u) - acos((double)u));
    if (eas > worstAsin) worstAsin = eas;
    if (eac > worstAsin) worstAsin = eac;
  }
  CHECK(worstSin <= 2e-6);
  CHECK(worstCos <= 2e-6);
  CHECK(worstAtan <= 2e-6);
  CHECK(worstAsin <= 2e-6);
  CHECK(worstTan <= 2e-6);
  // range reduction
  CHECK_NEAR(Mathf::Sin(100.0f), (float)sin(100.0), 1e-4f);
  CHECK_NEAR(Mathf::Cos(-37.5f), (float)cos(-37.5), 1e-4f);
  // atan2 quadrants
  CHECK_NEAR(Mathf::Atan2(1.0f, -1.0f), 2.35619449f, 2e-6f);
  CHECK_NEAR(Mathf::Atan2(-1.0f, -1.0f), -2.35619449f, 2e-6f);
  CHECK_NEAR(Mathf::Atan2(-1.0f, 0.0f), -1.57079633f, 2e-6f);
}

TEST(mathu_mathf_misc) {
  CHECK_NEAR(Mathf::DeltaAngle(350.0f, 10.0f), 20.0f, 1e-4f);
  CHECK_NEAR(Mathf::DeltaAngle(10.0f, 350.0f), -20.0f, 1e-4f);
  CHECK_NEAR(Mathf::DeltaAngle(0.0f, 180.0f), 180.0f, 1e-4f);
  CHECK_NEAR(Mathf::PingPong(0.5f, 1.0f), 0.5f, 1e-6f);
  CHECK_NEAR(Mathf::PingPong(1.5f, 1.0f), 0.5f, 1e-6f);
  CHECK_NEAR(Mathf::PingPong(2.25f, 1.0f), 0.25f, 1e-6f);
  CHECK_NEAR(Mathf::InverseLerp(10.0f, 20.0f, 15.0f), 0.5f, 1e-6f);
  CHECK_EQ(Mathf::InverseLerp(5.0f, 5.0f, 7.0f), 0.0f);
  CHECK_EQ(Mathf::LerpUnclamped(0.0f, 10.0f, 2.0f), 20.0f);
  CHECK_EQ(Mathf::RoundToInt(2.5f), 2);  // half to even
  CHECK_EQ(Mathf::RoundToInt(3.5f), 4);
  CHECK_EQ(Mathf::RoundToInt(-2.5f), -2);
  CHECK_EQ(Mathf::FloorToInt(-0.5f), -1);
  CHECK_EQ(Mathf::Sign(0.0f), 1.0f);
  CHECK_NEAR(Mathf::Pow(2.0f, 10.0f), 1024.0f, 1e-2f);
  CHECK_NEAR(Mathf::PI, 3.14159265f, 1e-7f);
  // SmoothDamp(0 -> 10, smoothTime 1, dt 0.1), two steps.
  Scalar vel = 0;
  Scalar v1 = Mathf::SmoothDamp(0.0f, 10.0f, vel, 1.0f, Mathf::Infinity, 0.1f);
  CHECK_NEAR(v1, 0.17263406f, 1e-5f);
  CHECK_NEAR(vel, 3.27578865f, 1e-4f);
  Scalar v2 = Mathf::SmoothDamp(v1, 10.0f, vel, 1.0f, Mathf::Infinity, 0.1f);
  CHECK_NEAR(v2, 0.61055765f, 1e-5f);
  CHECK_NEAR(vel, 5.36539563f, 1e-4f);
}

TEST(mathu_vector3) {
  Vector3 x(1, 0, 0), y(0, 1, 0);
  CHECK(Vector3::Cross(y, x) == Vector3(0, 0, -1));
  CHECK_NEAR(Vector3::Angle(x, y), 90.0f, 1e-4f);
  CHECK_NEAR(Vector3::Angle(Vector3(1, 1, 0), x), 45.0f, 1e-4f);
  CHECK_NEAR(Vector3::SignedAngle(x, y, Vector3(0, 0, 1)), 90.0f, 1e-4f);
  CHECK_NEAR(Vector3::SignedAngle(y, x, Vector3(0, 0, 1)), -90.0f, 1e-4f);
  Vector3 p = Vector3::Project(Vector3(3, 4, 0), x);
  CHECK_NEAR(p.x, 3.0f, 1e-6f);
  CHECK_NEAR(p.y, 0.0f, 1e-6f);
  Vector3 pp = Vector3::ProjectOnPlane(Vector3(3, 4, 5), y);
  CHECK(pp == Vector3(3, 0, 5));
  Vector3 r = Vector3::Reflect(Vector3(1, -1, 0), y);  // bounce off a floor
  CHECK(r == Vector3(1, 1, 0));
  Vector3 m = Vector3::MoveTowards(Vector3(0, 0, 0), Vector3(10, 0, 0), 3.0f);
  CHECK_NEAR(m.x, 3.0f, 1e-6f);
  CHECK_EQ(Vector2(3, 4).magnitude(), 5.0f);
  CHECK_NEAR(Vector4(1, 2, 2, 0).magnitude(), 3.0f, 1e-6f);
}

TEST(mathu_quaternion_euler) {
  Quaternion q = Quaternion::Euler(30, 45, 60);  // Unity: (0.3919038, 0.2005621, 0.3604234, 0.8223632)
  CHECK_NEAR(q.x, 0.3919038f, 2e-6f);
  CHECK_NEAR(q.y, 0.2005621f, 2e-6f);
  CHECK_NEAR(q.z, 0.3604234f, 2e-6f);
  CHECK_NEAR(q.w, 0.8223632f, 2e-6f);
  Vector3 e = q.eulerAngles();
  CHECK_NEAR(e.x, 30.0f, 1e-3f);
  CHECK_NEAR(e.y, 45.0f, 1e-3f);
  CHECK_NEAR(e.z, 60.0f, 1e-3f);
  Vector3 neg = Quaternion::Euler(-10, 0, 0).eulerAngles();
  CHECK_NEAR(neg.x, 350.0f, 1e-3f);
  CHECK_NEAR(Quaternion::Dot(q, q.Inverse() * q * q), 1.0f, 1e-5f);
}

TEST(mathu_quaternion_look_slerp) {
  // Looking along +X with up = +Y: forward -> +X, up stays +Y.
  Quaternion l = Quaternion::LookRotation(Vector3(1, 0, 0));
  Vector3 f = l * Vector3::forward(), u = l * Vector3::up();
  CHECK_NEAR(f.x, 1.0f, 1e-5f);
  CHECK_NEAR(f.z, 0.0f, 1e-5f);
  CHECK_NEAR(u.y, 1.0f, 1e-5f);
  // Same as Euler(0, 90, 0).
  CHECK_NEAR(Quaternion::Angle(l, Quaternion::Euler(0, 90, 0)), 0.0f, 1e-3f);
  // Looking down: Unity LookRotation(down, forward) pitches 90 degrees.
  Quaternion d = Quaternion::LookRotation(Vector3(0, -1, 0), Vector3(0, 0, 1));
  CHECK_NEAR(Quaternion::Angle(d, Quaternion::Euler(90, 0, 0)), 0.0f, 1e-3f);
  Quaternion t = Quaternion::LookRotation(Vector3(1, 2, 3).normalized(), Vector3::up());
  Vector3 tf = t * Vector3::forward();
  CHECK_NEAR(tf.x, 1.0f / sqrtf(14.0f), 1e-5f);
  CHECK_NEAR(tf.z, 3.0f / sqrtf(14.0f), 1e-5f);
  // Slerp midpoint = half rotation (Unity: (0, 0.3826834, 0, 0.9238795)).
  Quaternion s = Quaternion::Slerp(Quaternion::identity(), Quaternion::Euler(0, 90, 0), 0.5f);
  CHECK_NEAR(s.x, 0.0f, 1e-6f);
  CHECK_NEAR(s.y, 0.3826834f, 2e-6f);
  CHECK_NEAR(s.w, 0.9238795f, 2e-6f);
  // Slerp takes the short way around when dot < 0.
  Quaternion a = Quaternion::Euler(0, 10, 0), b = Quaternion::Euler(0, 350, 0);
  Quaternion mid = Quaternion::Slerp(a, b, 0.5f);
  CHECK_NEAR(Quaternion::Angle(mid, Quaternion::identity()), 0.0f, 1e-3f);
  Quaternion fr = Quaternion::FromToRotation(Vector3(1, 0, 0), Vector3(0, 1, 0));
  Vector3 fv = fr * Vector3(1, 0, 0);
  CHECK_NEAR(fv.y, 1.0f, 1e-5f);
  Quaternion rt = Quaternion::RotateTowards(Quaternion::identity(), Quaternion::Euler(0, 90, 0), 30.0f);
  CHECK_NEAR(Quaternion::Angle(rt, Quaternion::identity()), 30.0f, 1e-2f);
}

TEST(mathu_matrix) {
  Quaternion q = Quaternion::Euler(30, 45, 60);
  Matrix4x4 m = Matrix4x4::TRS(Vector3(1, 2, 3), q, Vector3(1, 2, 3));
  Vector3 p(0.5f, -1.0f, 2.0f);
  Vector3 want = q * Vector3(0.5f, -2.0f, 6.0f) + Vector3(1, 2, 3);
  Vector3 got = m.MultiplyPoint3x4(p);
  CHECK_NEAR(got.x, want.x, 1e-5f);
  CHECK_NEAR(got.y, want.y, 1e-5f);
  CHECK_NEAR(got.z, want.z, 1e-5f);
  Vector3 back = m.inverse().MultiplyPoint3x4(got);
  CHECK_NEAR(back.x, p.x, 1e-4f);
  CHECK_NEAR(back.y, p.y, 1e-4f);
  CHECK_NEAR(back.z, p.z, 1e-4f);
  CHECK_NEAR(m.determinant(), 6.0f, 1e-3f);
  Vector3 sc = m.lossyScale();
  CHECK_NEAR(sc.y, 2.0f, 1e-5f);
  CHECK_NEAR(Quaternion::Angle(m.rotationPart(), q), 0.0f, 1e-3f);
  Vector4 col3 = m.GetColumn(3);
  CHECK_EQ(col3.x, 1.0f);
  CHECK_EQ(col3.w, 1.0f);
  Matrix4 o = Matrix4::Ortho(-2, 2, -1, 1, 0, 10);
  Vector3 c = o.MultiplyPoint(Vector3(2, 1, 0));
  CHECK_NEAR(c.x, 1.0f, 1e-6f);
  CHECK_NEAR(c.y, 1.0f, 1e-6f);
  CHECK_NEAR(c.z, -1.0f, 1e-6f);
}

TEST(mathu_color_rect_bounds) {
  Color c = Color::Lerp(Color::black(), Color::white(), 0.5f);
  CHECK_NEAR(c.r, 0.5f, 1e-6f);
  Color32 c32 = Color32::FromColor(c);
  CHECK_EQ(c32.r, 128);  // 127.5 rounds half to even -> 128
  CHECK_EQ(Color32(255, 0, 0).packedABGR(), 0xFF0000FFu);
  Rect r(10, 20, 30, 40);
  CHECK(r.Contains(Vector2(10, 20)));
  CHECK(!r.Contains(Vector2(40, 20)));
  CHECK(r.Overlaps(Rect(39, 59, 5, 5)));
  CHECK(!r.Overlaps(Rect(40, 60, 5, 5)));
  CHECK_EQ(r.center().x, 25.0f);
  Bounds b(Vector3(0, 0, 0), Vector3(2, 2, 2));
  CHECK(b.Contains(Vector3(1, 1, 1)));
  b.Encapsulate(Vector3(5, 0, 0));
  CHECK_EQ(b.max().x, 5.0f);
  CHECK_EQ(b.min().x, -1.0f);
  CHECK(b.Intersects(Bounds(Vector3(4, 0, 0), Vector3(1, 1, 1))));
  CHECK_EQ(b.SqrDistance(Vector3(8, 0, 0)), 9.0f);
}

TEST(mathu_random_deterministic) {
  Random::InitState(42);
  u32 a[8];
  for (u32& v : a) v = Random::NextU32();
  Random::InitState(42);
  for (u32 v : a) CHECK_EQ(Random::NextU32(), v);
  Random::InitState(43);
  CHECK(Random::NextU32() != a[0]);
  // Golden: xorshift128 seeded as s[0]=42, s[i]=1812433253*(s[i-1]^(s[i-1]>>30))+i
  Random::InitState(42);
  CHECK_EQ(Random::NextU32(), 0xe87b8324u);
  CHECK_EQ(Random::NextU32(), 0xb54ee4aau);
  CHECK_EQ(Random::NextU32(), 0xce152ec8u);
  Random::InitState(7);
  for (int i = 0; i < 1000; ++i) {
    Scalar v = Random::value();
    CHECK(v >= 0.0f && v < 1.0f);
    i32 n = Random::Range(3, 6);
    CHECK(n >= 3 && n < 6);
    Scalar f = Random::Range(-2.0f, 2.0f);
    CHECK(f >= -2.0f && f <= 2.0f);
    CHECK_NEAR(Random::onUnitSphere().magnitude(), 1.0f, 1e-4f);
    CHECK(Random::insideUnitCircle().sqrMagnitude() <= 1.0f);
  }
}

// Golden values from an independent double-precision reference of the same algorithm (Perlin's permutation, 8 gradients).
TEST(mathu_perlin_noise_golden) {
  struct G { float x, y, v; };
  const G g[] = {{0, 0, 0.5f}, {0.5f, 0.5f, 0.25f}, {1.25f, 3.75f, 0.550042f}, {-2.3f, 4.1f, 0.398253f}, {10.7f, -8.2f, 0.720218f},
                 {100.123f, 200.456f, 0.311356f}, {0.1f, 0.9f, 0.449971f}, {-0.5f, -0.5f, 0.3125f}};
  for (const G& c : g) CHECK(Mathf::Abs(Mathf::PerlinNoise(c.x, c.y) - c.v) < 1e-4f);
  CHECK_EQ(Mathf::PerlinNoise(3.3f, 7.7f), Mathf::PerlinNoise(3.3f, 7.7f));  // deterministic
  CHECK(Mathf::Abs(Mathf::PerlinNoise(0.3f, 0.6f) - Mathf::PerlinNoise(256.3f, 0.6f)) < 1e-4f);  // period 256
  for (int i = 0; i < 2000; ++i) {
    const float n = Mathf::PerlinNoise(static_cast<float>(i) * 0.0731f - 50.0f, static_cast<float>(i) * 0.1337f);
    CHECK(n >= 0.0f && n <= 1.0f);
  }
}
