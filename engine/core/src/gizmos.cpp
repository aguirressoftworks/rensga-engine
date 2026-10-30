#include "rensga/core/gizmos.h"

#if RENSGA_GIZMOS
#include <cmath>
#endif

namespace rensga {

#if RENSGA_GIZMOS
namespace {
GizmoLine g_lines[GizmoDraw::kMaxLines];
GizmoTriangle g_tris[GizmoDraw::kMaxTriangles];
unsigned g_nLines = 0, g_nTris = 0, g_dropped = 0;
Color g_color = Color::white();
Matrix4 g_matrix = Matrix4::identity();

u32 pack(const Color& c) {
  auto q = [](Scalar v) { return static_cast<u32>(Mathf::Clamp01(v) * 255.0f + 0.5f); };
  return q(c.r) | (q(c.g) << 8) | (q(c.b) << 16) | (q(c.a) << 24);
}

void line(const Vector3& a, const Vector3& b) {
  if (g_nLines >= GizmoDraw::kMaxLines) { ++g_dropped; return; }
  const Vector3 wa = g_matrix.transformPoint(a), wb = g_matrix.transformPoint(b);
  GizmoLine& l = g_lines[g_nLines++];
  l.a[0] = wa.x; l.a[1] = wa.y; l.a[2] = wa.z;
  l.b[0] = wb.x; l.b[1] = wb.y; l.b[2] = wb.z;
  l.rgba = pack(g_color);
}

void tri(const Vector3& a, const Vector3& b, const Vector3& c) {
  if (g_nTris >= GizmoDraw::kMaxTriangles) { ++g_dropped; return; }
  const Vector3 w[3] = {g_matrix.transformPoint(a), g_matrix.transformPoint(b), g_matrix.transformPoint(c)};
  GizmoTriangle& t = g_tris[g_nTris++];
  for (int i = 0; i < 3; ++i) { t.p[i * 3] = w[i].x; t.p[i * 3 + 1] = w[i].y; t.p[i * 3 + 2] = w[i].z; }
  t.rgba = pack(g_color);
}

void quad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d) { tri(a, b, c); tri(a, c, d); }

void cubeCorners(const Vector3& c, const Vector3& s, Vector3 out[8]) {
  const Vector3 h = s * 0.5f;
  for (int i = 0; i < 8; ++i)
    out[i] = Vector3(c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z));
}

void circle(const Vector3& c, int axis, Scalar r) {
  const int kSeg = 32;
  Vector3 prev;
  for (int i = 0; i <= kSeg; ++i) {
    const float a = static_cast<float>(i) * 6.2831853f / kSeg, s = std::sin(a) * r, k = std::cos(a) * r;
    const Vector3 p = axis == 0 ? Vector3(c.x, c.y + s, c.z + k) : axis == 1 ? Vector3(c.x + s, c.y, c.z + k) : Vector3(c.x + s, c.y + k, c.z);
    if (i > 0) line(prev, p);
    prev = p;
  }
}
}  // namespace

Color Gizmos::color() { return g_color; }
void Gizmos::setColor(const Color& c) { g_color = c; }
Matrix4 Gizmos::matrix() { return g_matrix; }
void Gizmos::setMatrix(const Matrix4& m) { g_matrix = m; }

void Gizmos::DrawLine(const Vector3& from, const Vector3& to) { line(from, to); }
void Gizmos::DrawRay(const Vector3& from, const Vector3& direction) { line(from, from + direction); }

void Gizmos::DrawWireCube(const Vector3& center, const Vector3& size) {
  Vector3 p[8];
  cubeCorners(center, size, p);
  for (int i = 0; i < 8; ++i)
    for (int bit = 1; bit < 8; bit <<= 1)
      if (!(i & bit)) line(p[i], p[i | bit]);
}

void Gizmos::DrawCube(const Vector3& center, const Vector3& size) {
  Vector3 p[8];
  cubeCorners(center, size, p);
  quad(p[0], p[2], p[3], p[1]); quad(p[4], p[5], p[7], p[6]);  // -z, +z
  quad(p[0], p[1], p[5], p[4]); quad(p[2], p[6], p[7], p[3]);  // -y, +y
  quad(p[0], p[4], p[6], p[2]); quad(p[1], p[3], p[7], p[5]);  // -x, +x
}

void Gizmos::DrawWireSphere(const Vector3& center, Scalar radius) {
  for (int axis = 0; axis < 3; ++axis) circle(center, axis, radius);
}

void Gizmos::DrawSphere(const Vector3& center, Scalar radius) {
  const int kLat = 8, kLon = 12;
  auto pt = [&](int i, int j) {
    const float th = 3.1415927f * static_cast<float>(i) / kLat, ph = 6.2831853f * static_cast<float>(j) / kLon;
    return Vector3(center.x + radius * std::sin(th) * std::cos(ph), center.y + radius * std::cos(th),
                   center.z + radius * std::sin(th) * std::sin(ph));
  };
  for (int i = 0; i < kLat; ++i)
    for (int j = 0; j < kLon; ++j) {
      if (i > 0) tri(pt(i, j), pt(i + 1, j), pt(i, j + 1));
      if (i < kLat - 1) tri(pt(i, j + 1), pt(i + 1, j), pt(i + 1, j + 1));
    }
}

void Gizmos::DrawFrustum(const Vector3& center, Scalar fov, Scalar maxRange, Scalar minRange, Scalar aspect) {
  const float t = std::tan(fov * 0.5f * 0.017453293f);
  Vector3 n[4], f[4];
  const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  for (int i = 0; i < 4; ++i) {
    n[i] = Vector3(center.x + sx[i] * t * minRange * aspect, center.y + sy[i] * t * minRange, center.z + minRange);
    f[i] = Vector3(center.x + sx[i] * t * maxRange * aspect, center.y + sy[i] * t * maxRange, center.z + maxRange);
  }
  for (int i = 0; i < 4; ++i) { line(n[i], n[(i + 1) % 4]); line(f[i], f[(i + 1) % 4]); line(n[i], f[i]); }
}

namespace GizmoDraw {
void clear() { g_nLines = g_nTris = g_dropped = 0; g_color = Color::white(); g_matrix = Matrix4::identity(); }
unsigned lineCount() { return g_nLines; }
unsigned triangleCount() { return g_nTris; }
const GizmoLine* lines() { return g_lines; }
const GizmoTriangle* triangles() { return g_tris; }
unsigned dropped() { return g_dropped; }
}  // namespace GizmoDraw

#else
namespace GizmoDraw {
void clear() {}
unsigned lineCount() { return 0; }
unsigned triangleCount() { return 0; }
const GizmoLine* lines() { return nullptr; }
const GizmoTriangle* triangles() { return nullptr; }
unsigned dropped() { return 0; }
}  // namespace GizmoDraw
#endif

}  // namespace rensga
