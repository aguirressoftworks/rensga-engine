// MVP-09: Vector2/3, Quaternion, Matrix4, Color32 and a Mathf subset, all in Scalar.
// Matrix4 is column-major (m[col*4 + row]) with a right-handed, -Z-forward view, OpenGL style.
#pragma once
#include <math.h>

#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {

namespace Mathf {
constexpr Scalar Pi = 3.14159265358979f;
constexpr Scalar PI = Pi;
constexpr Scalar Deg2Rad = Pi / 180.0f;
constexpr Scalar Rad2Deg = 180.0f / Pi;
constexpr Scalar Epsilon = 1e-6f;
constexpr Scalar Infinity = 3.4028234e38f;  // FLT_MAX: the PS2 FPU has no infinity

// Determinism: trig is polynomial (+ - * / and sqrt only, no libm), see math.cpp. Accuracy <= 2e-6 abs.
Scalar Sin(Scalar x);
Scalar Cos(Scalar x);
Scalar Tan(Scalar x);
Scalar Asin(Scalar x);
Scalar Acos(Scalar x);
Scalar Atan(Scalar x);
Scalar Atan2(Scalar y, Scalar x);

inline Scalar Sqrt(Scalar x) { return sqrtf(x); }  // correctly rounded in IEEE-754
inline Scalar Abs(Scalar x) { return x < 0.0f ? -x : x; }
inline Scalar Floor(Scalar x) { return floorf(x); }
inline Scalar Ceil(Scalar x) { return ceilf(x); }
inline Scalar Pow(Scalar x, Scalar y) { return powf(x, y); }
inline Scalar Min(Scalar a, Scalar b) { return a < b ? a : b; }
inline Scalar Max(Scalar a, Scalar b) { return a > b ? a : b; }
inline Scalar Clamp(Scalar v, Scalar lo, Scalar hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline Scalar Clamp01(Scalar v) { return Clamp(v, 0.0f, 1.0f); }
inline Scalar Lerp(Scalar a, Scalar b, Scalar t) { return a + (b - a) * Clamp01(t); }
inline Scalar LerpUnclamped(Scalar a, Scalar b, Scalar t) { return a + (b - a) * t; }
inline Scalar InverseLerp(Scalar a, Scalar b, Scalar v) { return a != b ? Clamp01((v - a) / (b - a)) : 0.0f; }
inline Scalar Sign(Scalar v) { return v < 0.0f ? -1.0f : 1.0f; }  // Unity: Sign(0) == 1
inline bool Approximately(Scalar a, Scalar b) { return Abs(a - b) <= 1e-5f * Max(1.0f, Max(Abs(a), Abs(b))); }
inline Scalar MoveTowards(Scalar cur, Scalar target, Scalar maxDelta) {
  if (Abs(target - cur) <= maxDelta) return target;
  return cur + Sign(target - cur) * maxDelta;
}
inline Scalar Repeat(Scalar t, Scalar len) { return Clamp(t - Floor(t / len) * len, 0.0f, len); }
inline Scalar PingPong(Scalar t, Scalar len) {
  t = Repeat(t, len * 2.0f);
  return len - Abs(t - len);
}
inline Scalar DeltaAngle(Scalar current, Scalar target) {
  Scalar d = Repeat(target - current, 360.0f);
  return d > 180.0f ? d - 360.0f : d;
}
inline Scalar LerpAngle(Scalar a, Scalar b, Scalar t) {
  Scalar d = Repeat(b - a, 360.0f);
  if (d > 180.0f) d -= 360.0f;
  return a + d * Clamp01(t);
}
inline Scalar MoveTowardsAngle(Scalar cur, Scalar target, Scalar maxDelta) {
  Scalar d = DeltaAngle(cur, target);
  if (-maxDelta < d && d < maxDelta) return target;
  return MoveTowards(cur, cur + d, maxDelta);
}
// Round half to even, like Unity's Mathf.Round.
inline Scalar Round(Scalar x) {
  Scalar r = Floor(x), d = x - r;
  if (d > 0.5f) return r + 1.0f;
  if (d < 0.5f) return r;
  return Floor(r * 0.5f) * 2.0f == r ? r : r + 1.0f;
}
inline i32 FloorToInt(Scalar x) { return static_cast<i32>(Floor(x)); }
inline i32 CeilToInt(Scalar x) { return static_cast<i32>(Ceil(x)); }
inline i32 RoundToInt(Scalar x) { return static_cast<i32>(Round(x)); }
// Critically damped spring, same algorithm as Unity. deltaTime is explicit (math does not know Time).
inline Scalar SmoothDamp(Scalar current, Scalar target, Scalar& currentVelocity, Scalar smoothTime,
                         Scalar maxSpeed, Scalar deltaTime) {
  smoothTime = Max(0.0001f, smoothTime);
  Scalar omega = 2.0f / smoothTime, x = omega * deltaTime;
  Scalar e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
  Scalar change = current - target, originalTo = target;
  Scalar maxChange = maxSpeed * smoothTime;
  change = Clamp(change, -maxChange, maxChange);
  target = current - change;
  Scalar temp = (currentVelocity + omega * change) * deltaTime;
  currentVelocity = (currentVelocity - omega * temp) * e;
  Scalar out = target + (change + temp) * e;
  if ((originalTo - current > 0.0f) == (out > originalTo)) {
    out = originalTo;
    currentVelocity = (out - originalTo) / deltaTime;
  }
  return out;
}
inline Scalar SmoothDamp(Scalar current, Scalar target, Scalar& currentVelocity, Scalar smoothTime, Scalar deltaTime) {
  return SmoothDamp(current, target, currentVelocity, smoothTime, Infinity, deltaTime);
}
}  // namespace Mathf

struct Vector2 {
  Scalar x = 0, y = 0;
  constexpr Vector2() {}
  constexpr Vector2(Scalar x_, Scalar y_) : x(x_), y(y_) {}
  static constexpr Vector2 zero() { return {0, 0}; }
  static constexpr Vector2 one() { return {1, 1}; }
  static constexpr Vector2 up() { return {0, 1}; }
  static constexpr Vector2 right() { return {1, 0}; }
  Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
  Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
  Vector2 operator-() const { return {-x, -y}; }
  Vector2 operator*(Scalar s) const { return {x * s, y * s}; }
  Vector2 operator/(Scalar s) const { return {x / s, y / s}; }
  Vector2& operator+=(const Vector2& o) { x += o.x; y += o.y; return *this; }
  Vector2& operator-=(const Vector2& o) { x -= o.x; y -= o.y; return *this; }
  Vector2& operator*=(Scalar s) { x *= s; y *= s; return *this; }
  bool operator==(const Vector2& o) const { return x == o.x && y == o.y; }
  bool operator!=(const Vector2& o) const { return !(*this == o); }
  Scalar sqrMagnitude() const { return x * x + y * y; }
  Scalar magnitude() const { return Mathf::Sqrt(sqrMagnitude()); }
  Vector2 normalized() const {
    Scalar m = magnitude();
    return m > Mathf::Epsilon ? *this / m : Vector2();
  }
  static Scalar Dot(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }
  static Scalar Distance(const Vector2& a, const Vector2& b) { return (a - b).magnitude(); }
  static Vector2 Lerp(const Vector2& a, const Vector2& b, Scalar t) { return a + (b - a) * Mathf::Clamp01(t); }
  static Vector2 LerpUnclamped(const Vector2& a, const Vector2& b, Scalar t) { return a + (b - a) * t; }
  static Vector2 Scale(const Vector2& a, const Vector2& b) { return {a.x * b.x, a.y * b.y}; }
  static Vector2 Min(const Vector2& a, const Vector2& b) { return {Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y)}; }
  static Vector2 Max(const Vector2& a, const Vector2& b) { return {Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y)}; }
  static Scalar Angle(const Vector2& a, const Vector2& b) {
    Scalar d = Mathf::Sqrt(a.sqrMagnitude() * b.sqrMagnitude());
    if (d < 1e-15f) return 0;
    return Mathf::Acos(Mathf::Clamp(Dot(a, b) / d, -1.0f, 1.0f)) * Mathf::Rad2Deg;
  }
  static Vector2 MoveTowards(const Vector2& cur, const Vector2& target, Scalar maxDelta) {
    Vector2 d = target - cur;
    Scalar m = d.magnitude();
    if (m <= maxDelta || m == 0) return target;
    return cur + d / m * maxDelta;
  }
};
inline Vector2 operator*(Scalar s, const Vector2& v) { return v * s; }

struct Vector3 {
  Scalar x = 0, y = 0, z = 0;
  constexpr Vector3() {}
  constexpr Vector3(Scalar x_, Scalar y_, Scalar z_) : x(x_), y(y_), z(z_) {}
  static constexpr Vector3 zero() { return {0, 0, 0}; }
  static constexpr Vector3 one() { return {1, 1, 1}; }
  static constexpr Vector3 up() { return {0, 1, 0}; }
  static constexpr Vector3 down() { return {0, -1, 0}; }
  static constexpr Vector3 forward() { return {0, 0, 1}; }
  static constexpr Vector3 back() { return {0, 0, -1}; }
  static constexpr Vector3 right() { return {1, 0, 0}; }
  static constexpr Vector3 left() { return {-1, 0, 0}; }
  Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vector3 operator-() const { return {-x, -y, -z}; }
  Vector3 operator*(Scalar s) const { return {x * s, y * s, z * s}; }
  Vector3 operator/(Scalar s) const { return {x / s, y / s, z / s}; }
  Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vector3& operator*=(Scalar s) { x *= s; y *= s; z *= s; return *this; }
  Vector3& operator/=(Scalar s) { x /= s; y /= s; z /= s; return *this; }
  bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const Vector3& o) const { return !(*this == o); }
  Scalar sqrMagnitude() const { return x * x + y * y + z * z; }
  Scalar magnitude() const { return Mathf::Sqrt(sqrMagnitude()); }
  Vector3 normalized() const {
    Scalar m = magnitude();
    return m > Mathf::Epsilon ? *this / m : Vector3();
  }
  void Normalize() { *this = normalized(); }
  static Scalar Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  static Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  }
  static Scalar Distance(const Vector3& a, const Vector3& b) { return (a - b).magnitude(); }
  static Scalar Magnitude(const Vector3& a) { return a.magnitude(); }
  static Vector3 Lerp(const Vector3& a, const Vector3& b, Scalar t) { return a + (b - a) * Mathf::Clamp01(t); }
  static Vector3 LerpUnclamped(const Vector3& a, const Vector3& b, Scalar t) { return a + (b - a) * t; }
  static Vector3 Scale(const Vector3& a, const Vector3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
  static Vector3 Min(const Vector3& a, const Vector3& b) {
    return {Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y), Mathf::Min(a.z, b.z)};
  }
  static Vector3 Max(const Vector3& a, const Vector3& b) {
    return {Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y), Mathf::Max(a.z, b.z)};
  }
  static Vector3 MoveTowards(const Vector3& cur, const Vector3& target, Scalar maxDelta) {
    Vector3 d = target - cur;
    Scalar m = d.magnitude();
    if (m <= maxDelta || m == 0) return target;
    return cur + d / m * maxDelta;
  }
  // Degrees, 0..180.
  static Scalar Angle(const Vector3& a, const Vector3& b) {
    Scalar d = Mathf::Sqrt(a.sqrMagnitude() * b.sqrMagnitude());
    if (d < 1e-15f) return 0;
    return Mathf::Acos(Mathf::Clamp(Dot(a, b) / d, -1.0f, 1.0f)) * Mathf::Rad2Deg;
  }
  static Scalar SignedAngle(const Vector3& from, const Vector3& to, const Vector3& axis) {
    Scalar a = Angle(from, to);
    return Dot(axis, Cross(from, to)) < 0.0f ? -a : a;
  }
  static Vector3 Project(const Vector3& v, const Vector3& onNormal) {
    Scalar sq = Dot(onNormal, onNormal);
    if (sq < Mathf::Epsilon) return Vector3();
    return onNormal * (Dot(v, onNormal) / sq);
  }
  static Vector3 ProjectOnPlane(const Vector3& v, const Vector3& planeNormal) { return v - Project(v, planeNormal); }
  static Vector3 Reflect(const Vector3& inDir, const Vector3& normal) {
    return inDir - normal * (2.0f * Dot(normal, inDir));
  }
};
inline Vector3 operator*(Scalar s, const Vector3& v) { return v * s; }

struct Vector4 {
  Scalar x = 0, y = 0, z = 0, w = 0;
  constexpr Vector4() {}
  constexpr Vector4(Scalar x_, Scalar y_, Scalar z_, Scalar w_) : x(x_), y(y_), z(z_), w(w_) {}
  static constexpr Vector4 zero() { return {0, 0, 0, 0}; }
  static constexpr Vector4 one() { return {1, 1, 1, 1}; }
  Vector4 operator+(const Vector4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
  Vector4 operator-(const Vector4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
  Vector4 operator-() const { return {-x, -y, -z, -w}; }
  Vector4 operator*(Scalar s) const { return {x * s, y * s, z * s, w * s}; }
  Vector4 operator/(Scalar s) const { return {x / s, y / s, z / s, w / s}; }
  bool operator==(const Vector4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
  bool operator!=(const Vector4& o) const { return !(*this == o); }
  Scalar sqrMagnitude() const { return x * x + y * y + z * z + w * w; }
  Scalar magnitude() const { return Mathf::Sqrt(sqrMagnitude()); }
  Vector4 normalized() const {
    Scalar m = magnitude();
    return m > Mathf::Epsilon ? *this / m : Vector4();
  }
  static Scalar Dot(const Vector4& a, const Vector4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
  static Vector4 Lerp(const Vector4& a, const Vector4& b, Scalar t) { return a + (b - a) * Mathf::Clamp01(t); }
};
inline Vector4 operator*(Scalar s, const Vector4& v) { return v * s; }

struct Quaternion {
  Scalar x = 0, y = 0, z = 0, w = 1;
  constexpr Quaternion() {}
  constexpr Quaternion(Scalar x_, Scalar y_, Scalar z_, Scalar w_) : x(x_), y(y_), z(z_), w(w_) {}
  static constexpr Quaternion identity() { return {0, 0, 0, 1}; }
  static Quaternion AngleAxis(Scalar degrees, const Vector3& axis) {
    Vector3 a = axis.normalized();
    Scalar h = degrees * Mathf::Deg2Rad * 0.5f;
    Scalar s = Mathf::Sin(h);
    return {a.x * s, a.y * s, a.z * s, Mathf::Cos(h)};
  }
  // Unity order: rotate around Z, then X, then Y. Angles in degrees.
  static Quaternion Euler(Scalar xDeg, Scalar yDeg, Scalar zDeg) {
    Quaternion qx = AngleAxis(xDeg, {1, 0, 0}), qy = AngleAxis(yDeg, {0, 1, 0}), qz = AngleAxis(zDeg, {0, 0, 1});
    return qy * qx * qz;
  }
  static Quaternion Euler(const Vector3& e) { return Euler(e.x, e.y, e.z); }
  Quaternion operator*(const Quaternion& o) const {
    return {w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w, w * o.w - x * o.x - y * o.y - z * o.z};
  }
  Vector3 operator*(const Vector3& v) const {
    Vector3 q(x, y, z);
    Vector3 t = Vector3::Cross(q, v) * 2.0f;
    return v + t * w + Vector3::Cross(q, t);
  }
  Quaternion conjugate() const { return {-x, -y, -z, w}; }
  Scalar sqrMagnitude() const { return x * x + y * y + z * z + w * w; }
  Quaternion normalized() const {
    Scalar m = Mathf::Sqrt(sqrMagnitude());
    return m > Mathf::Epsilon ? Quaternion(x / m, y / m, z / m, w / m) : identity();
  }
  bool operator==(const Quaternion& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
  bool operator!=(const Quaternion& o) const { return !(*this == o); }

  Quaternion Inverse() const {
    Scalar n = sqrMagnitude();
    return n > 0 ? Quaternion(-x / n, -y / n, -z / n, w / n) : identity();
  }
  void Normalize() { *this = normalized(); }
  static Scalar Dot(const Quaternion& a, const Quaternion& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
  // Degrees between two rotations, 0..180.
  static Scalar Angle(const Quaternion& a, const Quaternion& b) {
    Scalar d = Mathf::Min(Mathf::Abs(Dot(a, b)), 1.0f);
    return d > 0.999999f ? 0.0f : Mathf::Acos(d) * 2.0f * Mathf::Rad2Deg;
  }
  // Normalized lerp along the shortest arc.
  static Quaternion LerpUnclamped(const Quaternion& a, const Quaternion& b, Scalar t) {
    Scalar s = Dot(a, b) < 0.0f ? -t : t;
    return Quaternion(a.x * (1.0f - t) + b.x * s, a.y * (1.0f - t) + b.y * s, a.z * (1.0f - t) + b.z * s,
                      a.w * (1.0f - t) + b.w * s).normalized();
  }
  static Quaternion Lerp(const Quaternion& a, const Quaternion& b, Scalar t) {
    return LerpUnclamped(a, b, Mathf::Clamp01(t));
  }
  static Quaternion SlerpUnclamped(const Quaternion& a, const Quaternion& b, Scalar t) {
    Quaternion bb = b;
    Scalar d = Dot(a, b);
    if (d < 0.0f) { bb = Quaternion(-b.x, -b.y, -b.z, -b.w); d = -d; }
    if (d > 0.9995f) return LerpUnclamped(a, bb, t);
    Scalar theta = Mathf::Acos(d), sn = Mathf::Sin(theta);
    Scalar s0 = Mathf::Sin((1.0f - t) * theta) / sn, s1 = Mathf::Sin(t * theta) / sn;
    return Quaternion(a.x * s0 + bb.x * s1, a.y * s0 + bb.y * s1, a.z * s0 + bb.z * s1, a.w * s0 + bb.w * s1);
  }
  static Quaternion Slerp(const Quaternion& a, const Quaternion& b, Scalar t) {
    return SlerpUnclamped(a, b, Mathf::Clamp01(t));
  }
  static Quaternion RotateTowards(const Quaternion& from, const Quaternion& to, Scalar maxDegrees) {
    Scalar ang = Angle(from, to);
    if (ang == 0.0f) return to;
    return SlerpUnclamped(from, to, Mathf::Min(1.0f, maxDegrees / ang));
  }
  static Quaternion FromToRotation(const Vector3& from, const Vector3& to) {
    Vector3 a = from.normalized(), b = to.normalized();
    Scalar d = Vector3::Dot(a, b);
    if (d > 0.999999f) return identity();
    if (d < -0.999999f) {
      Vector3 ax = Vector3::Cross(Mathf::Abs(a.x) < 0.9f ? Vector3::right() : Vector3::up(), a).normalized();
      return Quaternion(ax.x, ax.y, ax.z, 0);
    }
    Vector3 c = Vector3::Cross(a, b);
    return Quaternion(c.x, c.y, c.z, 1.0f + d).normalized();
  }
  // Orthonormal basis columns (right, up, forward) -> quaternion.
  static Quaternion FromBasis(const Vector3& c0, const Vector3& c1, const Vector3& c2) {
    Scalar m00 = c0.x, m10 = c0.y, m20 = c0.z, m01 = c1.x, m11 = c1.y, m21 = c1.z, m02 = c2.x, m12 = c2.y, m22 = c2.z;
    Scalar tr = m00 + m11 + m22;
    if (tr > 0) {
      Scalar s = Mathf::Sqrt(tr + 1.0f) * 2.0f;
      return Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
    }
    if (m00 > m11 && m00 > m22) {
      Scalar s = Mathf::Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
      return Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
    }
    if (m11 > m22) {
      Scalar s = Mathf::Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
      return Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
    }
    Scalar s = Mathf::Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    return Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
  }
  // Rotation whose +Z looks along `forward` with +Y as close to `up` as possible (left-handed, like Unity).
  static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3::up()) {
    if (forward.sqrMagnitude() < 1e-12f) return identity();
    Vector3 f = forward.normalized();
    Vector3 r = Vector3::Cross(up, f);
    if (r.sqrMagnitude() < 1e-12f) r = Vector3::Cross(Mathf::Abs(f.y) < 0.99f ? Vector3::up() : Vector3::right(), f);
    r = r.normalized();
    return FromBasis(r, Vector3::Cross(f, r), f);
  }
  // Unity eulerAngles: degrees in [0,360), applied Z then X then Y.
  Vector3 eulerAngles() const {
    Quaternion q = normalized();
    Scalar xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    Scalar r12 = 2 * (q.y * q.z - q.w * q.x), r20 = 2 * (q.x * q.z - q.w * q.y);
    Scalar ex, ey, ez;
    if (Mathf::Abs(r12) < 0.999999f) {
      ex = Mathf::Asin(-r12);
      ey = Mathf::Atan2(2 * (q.x * q.z + q.w * q.y), 1 - 2 * (xx + yy));
      ez = Mathf::Atan2(2 * (q.x * q.y + q.w * q.z), 1 - 2 * (xx + zz));
    } else {
      ex = r12 < 0 ? Mathf::Pi * 0.5f : -Mathf::Pi * 0.5f;
      ey = Mathf::Atan2(-r20, 1 - 2 * (yy + zz));
      ez = 0;
    }
    return {Mathf::Repeat(ex * Mathf::Rad2Deg, 360.0f), Mathf::Repeat(ey * Mathf::Rad2Deg, 360.0f),
            Mathf::Repeat(ez * Mathf::Rad2Deg, 360.0f)};
  }
};

struct alignas(16) Matrix4 {
  Scalar m[16];  // column-major: m[col * 4 + row]
  Matrix4() { *this = identity(); }
  static Matrix4 identity() {
    Matrix4 r(Uninit{});
    for (int i = 0; i < 16; ++i) r.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    return r;
  }
  static Matrix4 translation(const Vector3& t) {
    Matrix4 r = identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
  }
  static Matrix4 scaling(const Vector3& s) {
    Matrix4 r = identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
  }
  static Matrix4 rotation(const Quaternion& q) {
    Matrix4 r = identity();
    Scalar xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    Scalar xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    Scalar wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz);     r.m[2] = 2 * (xz - wy);
    r.m[4] = 2 * (xy - wz);     r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
    r.m[8] = 2 * (xz + wy);     r.m[9] = 2 * (yz - wx);     r.m[10] = 1 - 2 * (xx + yy);
    return r;
  }
  static Matrix4 TRS(const Vector3& t, const Quaternion& q, const Vector3& s) {
    Matrix4 r = rotation(q);
    r.m[0] *= s.x; r.m[1] *= s.x; r.m[2] *= s.x;
    r.m[4] *= s.y; r.m[5] *= s.y; r.m[6] *= s.y;
    r.m[8] *= s.z; r.m[9] *= s.z; r.m[10] *= s.z;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
  }
  // OpenGL-style perspective (clip z in -1..1). fovDegrees is vertical.
  static Matrix4 perspective(Scalar fovDegrees, Scalar aspect, Scalar nearClip, Scalar farClip) {
    Matrix4 r(Uninit{});
    for (int i = 0; i < 16; ++i) r.m[i] = 0;
    Scalar f = 1.0f / Mathf::Tan(fovDegrees * Mathf::Deg2Rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (farClip + nearClip) / (nearClip - farClip);
    r.m[11] = -1;
    r.m[14] = (2 * farClip * nearClip) / (nearClip - farClip);
    return r;
  }
  static Matrix4 lookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
    Vector3 f = (target - eye).normalized();
    Vector3 s = Vector3::Cross(f, up).normalized();
    Vector3 u = Vector3::Cross(s, f);
    Matrix4 r = identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -Vector3::Dot(s, eye); r.m[13] = -Vector3::Dot(u, eye); r.m[14] = Vector3::Dot(f, eye);
    return r;
  }
  Matrix4 operator*(const Matrix4& b) const {
    Matrix4 r(Uninit{});
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        Scalar s = 0;
        for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = s;
      }
    return r;
  }
  Vector3 transformPoint(const Vector3& p) const {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
  }
  Vector3 transformVector(const Vector3& p) const {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z,
            m[2] * p.x + m[6] * p.y + m[10] * p.z};
  }
  Vector3 translationPart() const { return {m[12], m[13], m[14]}; }
  Matrix4 transposed() const {
    Matrix4 r(Uninit{});
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) r.m[c * 4 + row] = m[row * 4 + c];
    return r;
  }
  // Unity-style names (forward to the lower-case builders above).
  static Matrix4 Translate(const Vector3& t) { return translation(t); }
  static Matrix4 Scale(const Vector3& s) { return scaling(s); }
  static Matrix4 Rotate(const Quaternion& q) { return rotation(q); }
  static Matrix4 Perspective(Scalar fov, Scalar aspect, Scalar n, Scalar f) { return perspective(fov, aspect, n, f); }
  static Matrix4 Ortho(Scalar l, Scalar r, Scalar b, Scalar t, Scalar n, Scalar f) {
    Matrix4 o = identity();
    o.m[0] = 2 / (r - l); o.m[5] = 2 / (t - b); o.m[10] = -2 / (f - n);
    o.m[12] = -(r + l) / (r - l); o.m[13] = -(t + b) / (t - b); o.m[14] = -(f + n) / (f - n);
    return o;
  }
  Vector3 MultiplyPoint3x4(const Vector3& p) const { return transformPoint(p); }
  Vector3 MultiplyVector(const Vector3& v) const { return transformVector(v); }
  // Full projective transform (divides by w).
  Vector3 MultiplyPoint(const Vector3& p) const {
    Scalar w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    Vector3 r = transformPoint(p);
    return Mathf::Abs(w) > 1e-30f ? r / w : Vector3();
  }
  Vector4 GetColumn(int c) const { return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]}; }
  Vector4 GetRow(int r) const { return {m[r], m[4 + r], m[8 + r], m[12 + r]}; }
  Vector3 lossyScale() const {
    return {Mathf::Sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]), Mathf::Sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]),
            Mathf::Sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10])};
  }
  Quaternion rotationPart() const {
    Vector3 s = lossyScale();
    if (s.x < 1e-12f || s.y < 1e-12f || s.z < 1e-12f) return Quaternion::identity();
    return Quaternion::FromBasis(Vector3(m[0], m[1], m[2]) / s.x, Vector3(m[4], m[5], m[6]) / s.y,
                                 Vector3(m[8], m[9], m[10]) / s.z);
  }
  Matrix4 Transpose() const { return transposed(); }
  Matrix4 Inverse() const { return inverse(); }
  Scalar determinant() const {
    const Scalar* a = m;
    Scalar c0 = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    Scalar c1 = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    Scalar c2 = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    Scalar c3 = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    return a[0] * c0 + a[1] * c1 + a[2] * c2 + a[3] * c3;
  }
  // General 4x4 inverse; returns identity if singular.
  Matrix4 inverse() const;

 private:
  struct Uninit {};
  explicit Matrix4(Uninit) {}
};

using Matrix4x4 = Matrix4;

struct Color32;

// Float color, 0..1 per channel.
struct Color {
  Scalar r = 1, g = 1, b = 1, a = 1;
  constexpr Color() {}
  constexpr Color(Scalar r_, Scalar g_, Scalar b_, Scalar a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
  static constexpr Color white() { return {1, 1, 1, 1}; }
  static constexpr Color black() { return {0, 0, 0, 1}; }
  static constexpr Color red() { return {1, 0, 0, 1}; }
  static constexpr Color green() { return {0, 1, 0, 1}; }
  static constexpr Color blue() { return {0, 0, 1, 1}; }
  static constexpr Color yellow() { return {1, 0.92156863f, 0.015686275f, 1}; }  // Unity's yellow
  static constexpr Color clear() { return {0, 0, 0, 0}; }
  Color operator+(const Color& o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
  Color operator-(const Color& o) const { return {r - o.r, g - o.g, b - o.b, a - o.a}; }
  Color operator*(Scalar s) const { return {r * s, g * s, b * s, a * s}; }
  Color operator*(const Color& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
  bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
  bool operator!=(const Color& o) const { return !(*this == o); }
  static Color Lerp(const Color& x, const Color& y, Scalar t) { return x + (y - x) * Mathf::Clamp01(t); }
  static Color LerpUnclamped(const Color& x, const Color& y, Scalar t) { return x + (y - x) * t; }
  Scalar grayscale() const { return 0.299f * r + 0.587f * g + 0.114f * b; }
};

struct Color32 {
  u8 r = 255, g = 255, b = 255, a = 255;
  constexpr Color32() {}
  constexpr Color32(u8 r_, u8 g_, u8 b_, u8 a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
  // ABGR packed, matches GU_COLOR_8888 on little-endian.
  constexpr u32 packedABGR() const {
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
  }
  bool operator==(const Color32& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
  bool operator!=(const Color32& o) const { return !(*this == o); }
  static Color32 FromColor(const Color& c) {
    auto q = [](Scalar v) { return static_cast<u8>(Mathf::Round(Mathf::Clamp01(v) * 255.0f)); };
    return Color32(q(c.r), q(c.g), q(c.b), q(c.a));
  }
  Color toColor() const { return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }
  static Color32 Lerp(const Color32& x, const Color32& y, Scalar t) { return FromColor(Color::Lerp(x.toColor(), y.toColor(), t)); }
};

// Axis-aligned rectangle; x,y is the min corner. Used for sprite source/destination rects.
struct Rect {
  Scalar x = 0, y = 0, width = 0, height = 0;
  constexpr Rect() {}
  constexpr Rect(Scalar x_, Scalar y_, Scalar w_, Scalar h_) : x(x_), y(y_), width(w_), height(h_) {}
  static Rect MinMaxRect(Scalar xMin, Scalar yMin, Scalar xMax, Scalar yMax) { return {xMin, yMin, xMax - xMin, yMax - yMin}; }
  Scalar xMin() const { return x; }
  Scalar yMin() const { return y; }
  Scalar xMax() const { return x + width; }
  Scalar yMax() const { return y + height; }
  Vector2 position() const { return {x, y}; }
  Vector2 size() const { return {width, height}; }
  Vector2 center() const { return {x + width * 0.5f, y + height * 0.5f}; }
  bool Contains(const Vector2& p) const { return p.x >= x && p.x < x + width && p.y >= y && p.y < y + height; }
  bool Overlaps(const Rect& o) const { return o.x < x + width && x < o.x + o.width && o.y < y + height && y < o.y + o.height; }
  bool operator==(const Rect& o) const { return x == o.x && y == o.y && width == o.width && height == o.height; }
  bool operator!=(const Rect& o) const { return !(*this == o); }
};

// Axis-aligned box stored as center + extents (half size).
struct Bounds {
  Vector3 center, extents;
  Bounds() {}
  Bounds(const Vector3& c, const Vector3& size) : center(c), extents(size * 0.5f) {}
  Vector3 size() const { return extents * 2.0f; }
  Vector3 min() const { return center - extents; }
  Vector3 max() const { return center + extents; }
  void SetMinMax(const Vector3& mn, const Vector3& mx) { extents = (mx - mn) * 0.5f; center = mn + extents; }
  bool Contains(const Vector3& p) const {
    Vector3 a = min(), b = max();
    return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y && p.z >= a.z && p.z <= b.z;
  }
  bool Intersects(const Bounds& o) const {
    Vector3 a = min(), b = max(), c = o.min(), d = o.max();
    return a.x <= d.x && b.x >= c.x && a.y <= d.y && b.y >= c.y && a.z <= d.z && b.z >= c.z;
  }
  void Encapsulate(const Vector3& p) { SetMinMax(Vector3::Min(min(), p), Vector3::Max(max(), p)); }
  void Encapsulate(const Bounds& o) { Encapsulate(o.min()); Encapsulate(o.max()); }
  void Expand(Scalar amount) { extents += Vector3(amount, amount, amount) * 0.5f; }
  Scalar SqrDistance(const Vector3& p) const {
    Vector3 a = min(), b = max();
    Vector3 c(Mathf::Max(Mathf::Max(a.x - p.x, p.x - b.x), 0.0f), Mathf::Max(Mathf::Max(a.y - p.y, p.y - b.y), 0.0f),
              Mathf::Max(Mathf::Max(a.z - p.z, p.z - b.z), 0.0f));
    return c.sqrMagnitude();
  }
  bool operator==(const Bounds& o) const { return center == o.center && extents == o.extents; }
};

// Deterministic xorshift128 (Marsaglia), one global stream like Unity's static Random. Same seed, same sequence on
// every platform. No libm: sphere sampling uses Mathf::Sin/Cos/Sqrt.
struct Random {
  static void InitState(u32 seed);
  static u32 NextU32();
  static Scalar value();                       // [0, 1), 24 random bits
  static Scalar Range(Scalar minInclusive, Scalar maxInclusive);
  static i32 Range(i32 minInclusive, i32 maxExclusive);
  static Vector2 insideUnitCircle();
  static Vector3 onUnitSphere();
  static Vector3 insideUnitSphere();
};

}  // namespace rensga
