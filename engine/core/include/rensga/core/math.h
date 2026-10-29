// MVP-09: Vector2/3, Quaternion, Matrix4, Color32 and a Mathf subset, all in Scalar.
// Matrix4 is column-major (m[col*4 + row]) with a right-handed, -Z-forward view, OpenGL style.
#pragma once
#include <math.h>

#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {

namespace Mathf {
constexpr Scalar Pi = 3.14159265358979f;
constexpr Scalar Deg2Rad = Pi / 180.0f;
constexpr Scalar Rad2Deg = 180.0f / Pi;
constexpr Scalar Epsilon = 1e-6f;

inline Scalar Sin(Scalar x) { return sinf(x); }
inline Scalar Cos(Scalar x) { return cosf(x); }
inline Scalar Tan(Scalar x) { return tanf(x); }
inline Scalar Asin(Scalar x) { return asinf(x); }
inline Scalar Acos(Scalar x) { return acosf(x); }
inline Scalar Atan2(Scalar y, Scalar x) { return atan2f(y, x); }
inline Scalar Sqrt(Scalar x) { return sqrtf(x); }
inline Scalar Abs(Scalar x) { return fabsf(x); }
inline Scalar Floor(Scalar x) { return floorf(x); }
inline Scalar Ceil(Scalar x) { return ceilf(x); }
inline Scalar Pow(Scalar x, Scalar y) { return powf(x, y); }
inline Scalar Min(Scalar a, Scalar b) { return a < b ? a : b; }
inline Scalar Max(Scalar a, Scalar b) { return a > b ? a : b; }
inline Scalar Clamp(Scalar v, Scalar lo, Scalar hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline Scalar Clamp01(Scalar v) { return Clamp(v, 0.0f, 1.0f); }
inline Scalar Lerp(Scalar a, Scalar b, Scalar t) { return a + (b - a) * Clamp01(t); }
inline Scalar Sign(Scalar v) { return v < 0.0f ? -1.0f : 1.0f; }
inline bool Approximately(Scalar a, Scalar b) { return Abs(a - b) <= 1e-5f * Max(1.0f, Max(Abs(a), Abs(b))); }
inline Scalar MoveTowards(Scalar cur, Scalar target, Scalar maxDelta) {
  if (Abs(target - cur) <= maxDelta) return target;
  return cur + Sign(target - cur) * maxDelta;
}
inline Scalar Repeat(Scalar t, Scalar len) { return t - Floor(t / len) * len; }
}  // namespace Mathf

struct Vector2 {
  Scalar x = 0, y = 0;
  constexpr Vector2() {}
  constexpr Vector2(Scalar x_, Scalar y_) : x(x_), y(y_) {}
  Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
  Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
  Vector2 operator*(Scalar s) const { return {x * s, y * s}; }
  Scalar sqrMagnitude() const { return x * x + y * y; }
  Scalar magnitude() const { return Mathf::Sqrt(sqrMagnitude()); }
};

struct Vector3 {
  Scalar x = 0, y = 0, z = 0;
  constexpr Vector3() {}
  constexpr Vector3(Scalar x_, Scalar y_, Scalar z_) : x(x_), y(y_), z(z_) {}
  static constexpr Vector3 zero() { return {0, 0, 0}; }
  static constexpr Vector3 one() { return {1, 1, 1}; }
  static constexpr Vector3 up() { return {0, 1, 0}; }
  static constexpr Vector3 forward() { return {0, 0, 1}; }
  static constexpr Vector3 right() { return {1, 0, 0}; }
  Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vector3 operator-() const { return {-x, -y, -z}; }
  Vector3 operator*(Scalar s) const { return {x * s, y * s, z * s}; }
  Vector3 operator/(Scalar s) const { return {x / s, y / s, z / s}; }
  Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vector3& operator*=(Scalar s) { x *= s; y *= s; z *= s; return *this; }
  bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const Vector3& o) const { return !(*this == o); }
  Scalar sqrMagnitude() const { return x * x + y * y + z * z; }
  Scalar magnitude() const { return Mathf::Sqrt(sqrMagnitude()); }
  Vector3 normalized() const {
    Scalar m = magnitude();
    return m > Mathf::Epsilon ? *this / m : Vector3();
  }
  static Scalar Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  static Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  }
  static Scalar Distance(const Vector3& a, const Vector3& b) { return (a - b).magnitude(); }
  static Vector3 Lerp(const Vector3& a, const Vector3& b, Scalar t) { return a + (b - a) * Mathf::Clamp01(t); }
};
inline Vector3 operator*(Scalar s, const Vector3& v) { return v * s; }

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
  // General 4x4 inverse; returns identity if singular.
  Matrix4 inverse() const;

 private:
  struct Uninit {};
  explicit Matrix4(Uninit) {}
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
};

}  // namespace rensga
