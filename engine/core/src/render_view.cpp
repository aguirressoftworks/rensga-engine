// F6.09 (B2): camera views and screen <-> world conversions, the shared fallback for quads a renderer cannot draw in 3D.
#include "rensga/core/render_view.h"

#include "rensga/core/gameplay.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {
u32 g_screenW = 480, g_screenH = 272;
#ifdef RENSGA_RELEASE
bool g_debugDraw = false;
#else
bool g_debugDraw = true;
#endif

i32 roundToInt(Scalar v) { return static_cast<i32>(v + (v < 0 ? -0.5f : 0.5f)); }
Scalar clamp01(Scalar v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
}  // namespace

namespace RenderScreen {
void set(u32 w, u32 h) {
  if (w && h) { g_screenW = w; g_screenH = h; }
}
u32 width() { return g_screenW; }
u32 height() { return g_screenH; }
}  // namespace RenderScreen

void setDebugDraw(bool enabled) { g_debugDraw = enabled; }
bool debugDrawEnabled() { return g_debugDraw; }

CameraView makeCameraView(const Camera& cam, u32 screenW, u32 screenH) {
  CameraView v;
  if (!cam.ownerWorld() || !cam.transform()) return v;  // a Camera outside a World has no view (w = h = 0): the queries return zeros
  const Matrix4 world = cam.transform()->localToWorld();
  v.view = world.inverse();
  const Scalar fw = static_cast<Scalar>(screenW), fh = static_cast<Scalar>(screenH);
  const Scalar vx = clamp01(cam.viewportX), vy = clamp01(cam.viewportY);
  Scalar vw = clamp01(cam.viewportW), vh = clamp01(cam.viewportH);
  if (vx + vw > 1) vw = 1 - vx;
  if (vy + vh > 1) vh = 1 - vy;
  const i32 x0 = roundToInt(vx * fw), x1 = roundToInt((vx + vw) * fw);
  const i32 yb0 = roundToInt(vy * fh), yb1 = roundToInt((vy + vh) * fh);  // from the bottom
  v.x = x0;
  v.w = x1 - x0;
  v.y = static_cast<i32>(screenH) - yb1;
  v.h = yb1 - yb0;
  v.full = v.x == 0 && v.y == 0 && v.w == static_cast<i32>(screenW) && v.h == static_cast<i32>(screenH);
  const Scalar aspect = v.full ? (screenH ? fw / fh : 1.0f) : (v.h > 0 ? static_cast<Scalar>(v.w) / static_cast<Scalar>(v.h) : 1.0f);
  v.nearClip = cam.nearClip;
  v.farClip = cam.farClip;
  v.ortho = cam.orthographic;
  if (v.ortho) {
    v.orthoHalfHeight = cam.orthographicSize;
    const Scalar hh = cam.orthographicSize, hw = hh * aspect;
    v.proj = Matrix4::Ortho(-hw, hw, -hh, hh, cam.nearClip, cam.farClip);
  } else {
    v.tanHalfFov = Mathf::Tan(cam.fov * Mathf::Deg2Rad * 0.5f);
    v.proj = Matrix4::perspective(cam.fov, aspect, cam.nearClip, cam.farClip);
  }
  v.position = world.translationPart();
  v.right = Vector3(world.m[0], world.m[1], world.m[2]).normalized();
  v.up = Vector3(world.m[4], world.m[5], world.m[6]).normalized();
  v.forward = Vector3(-world.m[8], -world.m[9], -world.m[10]).normalized();
  return v;
}

namespace {
void ndcFromScreen(const CameraView& v, Scalar sx, Scalar sy, Scalar& nx, Scalar& ny) {
  const Scalar w = v.w > 0 ? static_cast<Scalar>(v.w) : 1.0f, h = v.h > 0 ? static_cast<Scalar>(v.h) : 1.0f;
  nx = (sx - static_cast<Scalar>(v.x)) / w * 2.0f - 1.0f;
  ny = 1.0f - (sy - static_cast<Scalar>(v.y)) / h * 2.0f;
}
Ray rayFromNdc(const CameraView& v, Scalar nx, Scalar ny) {
  const Matrix4 inv = (v.proj * v.view).inverse();
  const Vector3 p0 = inv.MultiplyPoint(Vector3(nx, ny, -1.0f));
  const Vector3 p1 = inv.MultiplyPoint(Vector3(nx, ny, 1.0f));
  Ray r;
  r.direction = (p1 - p0).normalized();
  r.origin = v.ortho ? p0 : v.position;
  return r;
}
Vector3 pointFromNdc(const CameraView& v, Scalar nx, Scalar ny, Scalar z) {
  const Ray r = rayFromNdc(v, nx, ny);
  if (v.ortho) return r.origin + v.forward * (z - v.nearClip);  // origin sits on the near plane
  const Scalar c = Vector3::Dot(r.direction, v.forward);
  return r.origin + r.direction * (Mathf::Abs(c) > 1e-6f ? z / c : z);
}
// Normalized device coordinates of a world point; false when it is on the eye plane (w == 0).
bool ndcOfWorld(const CameraView& v, const Vector3& p, Scalar& nx, Scalar& ny) {
  const Matrix4 vp = v.proj * v.view;
  const Scalar w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
  if (Mathf::Abs(w) < 1e-9f) return false;
  const Vector3 c = vp.transformPoint(p);
  nx = c.x / w;
  ny = c.y / w;
  return true;
}
}  // namespace

Vector3 viewScreenToWorld(const CameraView& v, const Vector3& sp) {
  if (v.w <= 0 || v.h <= 0) return Vector3();
  Scalar nx, ny;
  ndcFromScreen(v, sp.x, sp.y, nx, ny);
  return pointFromNdc(v, nx, ny, sp.z);
}

Vector3 viewWorldToScreen(const CameraView& v, const Vector3& p) {
  if (v.w <= 0 || v.h <= 0) return Vector3();
  Scalar nx, ny;
  if (!ndcOfWorld(v, p, nx, ny)) return Vector3();
  return Vector3(static_cast<Scalar>(v.x) + (nx * 0.5f + 0.5f) * static_cast<Scalar>(v.w),
                 static_cast<Scalar>(v.y) + (1.0f - (ny * 0.5f + 0.5f)) * static_cast<Scalar>(v.h), Vector3::Dot(p - v.position, v.forward));
}

Ray viewScreenRay(const CameraView& v, const Vector2& sp) {
  if (v.w <= 0 || v.h <= 0) return Ray();
  Scalar nx, ny;
  ndcFromScreen(v, sp.x, sp.y, nx, ny);
  return rayFromNdc(v, nx, ny);
}

Vector3 viewWorldToViewport(const CameraView& v, const Vector3& p) {
  if (v.w <= 0 || v.h <= 0) return Vector3();
  Scalar nx, ny;
  if (!ndcOfWorld(v, p, nx, ny)) return Vector3();
  return Vector3(nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, Vector3::Dot(p - v.position, v.forward));
}

Vector3 viewViewportToWorld(const CameraView& v, const Vector3& vp) {
  if (v.w <= 0 || v.h <= 0) return Vector3();
  return pointFromNdc(v, vp.x * 2.0f - 1.0f, vp.y * 2.0f - 1.0f, vp.z);
}

Ray viewViewportRay(const CameraView& v, const Vector2& vp) {
  if (v.w <= 0 || v.h <= 0) return Ray();
  return rayFromNdc(v, vp.x * 2.0f - 1.0f, vp.y * 2.0f - 1.0f);
}

Scalar viewPixelSize(const CameraView& v, const Vector3& point, Scalar pixels) {
  const Scalar h = v.h > 0 ? static_cast<Scalar>(v.h) : 1.0f;
  if (v.ortho) return pixels * 2.0f * v.orthoHalfHeight / h;
  Scalar d = Vector3::Dot(point - v.position, v.forward);
  if (d < v.nearClip) d = v.nearClip;
  return pixels * 2.0f * v.tanHalfFov * d / h;
}

void submitQuads(IRenderer& r, const CameraView& v, u32 tex, u32 texW, u32 texH, const Quad3D* q, u32 n) {
  if (!n) return;
  if (r.drawQuads3D(tex, q, n, 0)) return;
  // The renderer has no 3D quads (GS, Null): axis-aligned quads (camera-facing billboards seen from an upright camera, as in a
  // 2D game with an orthographic camera) become screen rectangles / sprites; rotated ones (line ribbons) are skipped.
  const u32 key = kSpriteSortBase + 0x800;
  for (u32 i = 0; i < n; ++i) {
    const Quad3D& a = q[i];
    const Vector3 s0 = viewWorldToScreen(v, a.p[0]), s1 = viewWorldToScreen(v, a.p[1]), s2 = viewWorldToScreen(v, a.p[2]),
                  s3 = viewWorldToScreen(v, a.p[3]);
    if (s0.z < v.nearClip || s2.z < v.nearClip) continue;
    if (Mathf::Abs(s1.y - s0.y) > 1.0f || Mathf::Abs(s3.x - s0.x) > 1.0f) continue;
    const Scalar x0 = s0.x < s2.x ? s0.x : s2.x, x1 = s0.x < s2.x ? s2.x : s0.x;
    const Scalar y0 = s0.y < s2.y ? s0.y : s2.y, y1 = s0.y < s2.y ? s2.y : s0.y;
    const Rect dst(x0, y0, x1 - x0, y1 - y0);
    if (tex && texW && texH) {
      r.drawSprite(tex, Rect(a.u0 * static_cast<Scalar>(texW), a.v0 * static_cast<Scalar>(texH), (a.u1 - a.u0) * static_cast<Scalar>(texW),
                             (a.v1 - a.v0) * static_cast<Scalar>(texH)), dst, a.color, key);
    } else {
      r.drawRect(dst, a.color, key);
    }
  }
}

}  // namespace rensga
