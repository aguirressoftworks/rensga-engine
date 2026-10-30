#include "rensga/core/math.h"

namespace rensga {

// ---- Deterministic Mathf trig: polynomials with + - * / and sqrt only (no libm), so every platform and
// ---- compiler gives the same bits when built with -ffp-contract=off. Max abs error <= 2e-6 (tested).
namespace {
constexpr Scalar kHalfPi = 1.57079632679489662f;
constexpr Scalar kInvTwoPi = 0.15915494309189535f;
constexpr Scalar kTwoPiHi = 6.28125f;  // exactly representable; kTwoPiLo carries the rest (Cody-Waite)
constexpr Scalar kTwoPiLo = 0.0019353071795864769f;

inline Scalar reduceTwoPi(Scalar x) {  // to [-pi, pi]
  Scalar k = Mathf::Floor(x * kInvTwoPi + 0.5f);
  return (x - k * kTwoPiHi) - k * kTwoPiLo;
}
// Taylor on [-pi/2, pi/2], degree 11.
inline Scalar sinCore(Scalar x) {
  Scalar x2 = x * x;
  Scalar p = -1.0f / 39916800.0f;
  p = p * x2 + 1.0f / 362880.0f;
  p = p * x2 - 1.0f / 5040.0f;
  p = p * x2 + 1.0f / 120.0f;
  p = p * x2 - 1.0f / 6.0f;
  return x + x * x2 * p;
}
// Taylor on [-pi/2, pi/2], degree 12.
inline Scalar cosCore(Scalar x) {
  Scalar x2 = x * x;
  Scalar p = 1.0f / 479001600.0f;
  p = p * x2 - 1.0f / 3628800.0f;
  p = p * x2 + 1.0f / 40320.0f;
  p = p * x2 - 1.0f / 720.0f;
  p = p * x2 + 1.0f / 24.0f;
  p = p * x2 - 0.5f;
  return 1.0f + x2 * p;
}
// atan on [0, tan(15 deg)], odd Taylor to x^13.
inline Scalar atanCore(Scalar x) {
  Scalar x2 = x * x;
  Scalar p = 1.0f / 13.0f;
  p = p * x2 - 1.0f / 11.0f;
  p = p * x2 + 1.0f / 9.0f;
  p = p * x2 - 1.0f / 7.0f;
  p = p * x2 + 1.0f / 5.0f;
  p = p * x2 - 1.0f / 3.0f;
  return x + x * x2 * p;
}
// atan for x >= 0.
inline Scalar atanPos(Scalar x) {
  bool inv = x > 1.0f;
  if (inv) x = 1.0f / x;
  constexpr Scalar kSqrt3 = 1.73205080756887729f, kTan15 = 0.26794919243112270f, kPi6 = 0.52359877559829887f;
  Scalar r;
  if (x > kTan15) r = kPi6 + atanCore((x * kSqrt3 - 1.0f) / (kSqrt3 + x));
  else r = atanCore(x);
  return inv ? kHalfPi - r : r;
}
}  // namespace

namespace Mathf {
Scalar Sin(Scalar x) {
  x = reduceTwoPi(x);
  if (x > kHalfPi) x = Pi - x;
  else if (x < -kHalfPi) x = -Pi - x;
  return sinCore(x);
}
Scalar Cos(Scalar x) {
  x = reduceTwoPi(x);
  if (x < 0) x = -x;
  if (x > kHalfPi) return -cosCore(Pi - x);
  return cosCore(x);
}
Scalar Tan(Scalar x) { return Sin(x) / Cos(x); }
Scalar Atan(Scalar x) { return x < 0 ? -atanPos(-x) : atanPos(x); }
Scalar Atan2(Scalar y, Scalar x) {
  if (x == 0.0f) return y > 0.0f ? kHalfPi : (y < 0.0f ? -kHalfPi : 0.0f);
  Scalar a = Atan(y / x);
  if (x > 0.0f) return a;
  return y >= 0.0f ? a + Pi : a - Pi;
}
Scalar Asin(Scalar x) {
  x = Clamp(x, -1.0f, 1.0f);
  return Atan2(x, Sqrt((1.0f - x) * (1.0f + x)));
}
Scalar Acos(Scalar x) {
  x = Clamp(x, -1.0f, 1.0f);
  return Atan2(Sqrt((1.0f - x) * (1.0f + x)), x);
}
}  // namespace Mathf

// ---- Random: xorshift128, seeded like Mersenne init so small seeds give well mixed state.
namespace {
u32 g_rs[4] = {123456789u, 362436069u, 521288629u, 88675123u};
}
void Random::InitState(u32 seed) {
  g_rs[0] = seed;
  for (u32 i = 1; i < 4; ++i) g_rs[i] = 1812433253u * (g_rs[i - 1] ^ (g_rs[i - 1] >> 30)) + i;
}
u32 Random::NextU32() {
  u32 t = g_rs[0] ^ (g_rs[0] << 11);
  g_rs[0] = g_rs[1]; g_rs[1] = g_rs[2]; g_rs[2] = g_rs[3];
  g_rs[3] = (g_rs[3] ^ (g_rs[3] >> 19)) ^ (t ^ (t >> 8));
  return g_rs[3];
}
Scalar Random::value() { return static_cast<Scalar>(NextU32() >> 8) * (1.0f / 16777216.0f); }
Scalar Random::Range(Scalar a, Scalar b) { return a + value() * (b - a); }
i32 Random::Range(i32 a, i32 b) {
  if (b <= a) return a;
  return a + static_cast<i32>(NextU32() % static_cast<u32>(b - a));
}
Vector2 Random::insideUnitCircle() {
  for (;;) {
    Vector2 p(value() * 2.0f - 1.0f, value() * 2.0f - 1.0f);
    if (p.sqrMagnitude() <= 1.0f) return p;
  }
}
Vector3 Random::onUnitSphere() {
  Scalar z = value() * 2.0f - 1.0f, phi = value() * (2.0f * Mathf::Pi);
  Scalar r = Mathf::Sqrt(Mathf::Max(0.0f, 1.0f - z * z));
  return {r * Mathf::Cos(phi), r * Mathf::Sin(phi), z};
}
Vector3 Random::insideUnitSphere() {
  for (;;) {
    Vector3 p(value() * 2.0f - 1.0f, value() * 2.0f - 1.0f, value() * 2.0f - 1.0f);
    if (p.sqrMagnitude() <= 1.0f) return p;
  }
}

Matrix4 Matrix4::inverse() const {
  const Scalar* a = m;
  Scalar inv[16];
  inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
  inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
  inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
  inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
  inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
  inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
  inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
  inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
  inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
  inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
  inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
  inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
  inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
  inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
  inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
  inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
  Scalar det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
  if (Mathf::Abs(det) < 1e-12f) return identity();
  Matrix4 r;
  Scalar id = 1.0f / det;
  for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * id;
  return r;
}

}  // namespace rensga
