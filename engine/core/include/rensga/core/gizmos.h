// F3.01 (Editor 2): the Unity Gizmos API. Calls append to a fixed host draw list (static arrays, no heap, no per-frame
// allocation); the editor's Scene view clears the list each frame, collects built-in and script gizmos, and renders it.
// In console runtime builds (PSP, PS2, PSX are all MIPS) every call compiles to nothing and the list does not exist.
// Define RENSGA_GIZMOS=0/1 to override. Units are world units; the world is left-handed, +Y up (the engine's).
#pragma once
#include "rensga/core/math.h"
#include "rensga/core/types.h"

#ifndef RENSGA_GIZMOS
#if defined(__mips__) || defined(__PSP__) || defined(_EE) || defined(PSX)
#define RENSGA_GIZMOS 0
#else
#define RENSGA_GIZMOS 1
#endif
#endif

namespace rensga {

// One line or filled triangle in world space, color as packed RGBA8 (r in the low byte).
struct GizmoLine { float a[3], b[3]; u32 rgba; };
struct GizmoTriangle { float p[9]; u32 rgba; };

class Gizmos {
 public:
  // Unity: Gizmos.color / Gizmos.matrix. Both apply to the calls that follow; reset to white / identity each frame.
  static Color color();
  static void setColor(const Color& c);
  static Matrix4 matrix();
  static void setMatrix(const Matrix4& m);

  static void DrawLine(const Vector3& from, const Vector3& to);
  static void DrawRay(const Vector3& from, const Vector3& direction);
  static void DrawWireCube(const Vector3& center, const Vector3& size);
  static void DrawCube(const Vector3& center, const Vector3& size);
  static void DrawWireSphere(const Vector3& center, Scalar radius);
  static void DrawSphere(const Vector3& center, Scalar radius);
  // Camera frustum looking down +Z from `center` (apply setMatrix for a rotated one).
  static void DrawFrustum(const Vector3& center, Scalar fov, Scalar maxRange, Scalar minRange, Scalar aspect);
};

// Host side of the list: the Scene view reads it. Always declared; empty and inert when RENSGA_GIZMOS is 0.
namespace GizmoDraw {
constexpr unsigned kMaxLines = 32768;
constexpr unsigned kMaxTriangles = 8192;
void clear();                      // start of a frame: empties the list, resets color and matrix
unsigned lineCount();
unsigned triangleCount();
const GizmoLine* lines();
const GizmoTriangle* triangles();
unsigned dropped();                // primitives that did not fit since the last clear()
}  // namespace GizmoDraw

#if !RENSGA_GIZMOS
inline Color Gizmos::color() { return Color::white(); }
inline void Gizmos::setColor(const Color&) {}
inline Matrix4 Gizmos::matrix() { return Matrix4::identity(); }
inline void Gizmos::setMatrix(const Matrix4&) {}
inline void Gizmos::DrawLine(const Vector3&, const Vector3&) {}
inline void Gizmos::DrawRay(const Vector3&, const Vector3&) {}
inline void Gizmos::DrawWireCube(const Vector3&, const Vector3&) {}
inline void Gizmos::DrawCube(const Vector3&, const Vector3&) {}
inline void Gizmos::DrawWireSphere(const Vector3&, Scalar) {}
inline void Gizmos::DrawSphere(const Vector3&, Scalar) {}
inline void Gizmos::DrawFrustum(const Vector3&, Scalar, Scalar, Scalar, Scalar) {}
#endif

}  // namespace rensga
