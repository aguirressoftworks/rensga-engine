// F6.09 (B2, API 7): camera views, CPU particles and ribbons (LineRenderer, Debug::DrawLine). Everything here is visual: nothing in
// this file is read by the simulation, so the determinism hashes do not depend on it.
//
//   * CameraView: the matrices and the pixel rectangle of one Camera (perspective or orthographic, viewport rect). The Camera API
//     (Camera::ScreenToWorldPoint ...) and renderWorld use the same function, so what the player sees and what a script computes agree.
//   * Particles: one fixed-capacity pool for the whole program (kPoolCapacity particles, no heap), a block of `maxParticles` per
//     ParticleSystem, simulated in world space once per fixed step and drawn as camera-facing Quad3Ds (IRenderer::drawQuads3D).
//   * Ribbons: LineRenderer and Debug::DrawLine lines are camera-facing quads too.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/components.h"
#include "rensga/core/render_components.h"

namespace rensga {

class World;

enum class CameraClear : i32 { Skybox = 0, SolidColor = 1, DepthOnly = 2, Nothing = 3 };

struct CameraView {
  Matrix4 view, proj;           // view = inverse of the camera's world matrix; proj = perspective or orthographic (OpenGL style)
  i32 x = 0, y = 0, w = 0, h = 0;  // pixel rectangle on the screen, origin top-left
  bool full = true;             // covers the whole screen
  bool ortho = false;
  Scalar tanHalfFov = 0;        // perspective: tan(fov / 2); orthographic: 0
  Scalar orthoHalfHeight = 0;   // orthographic: orthographicSize
  Scalar nearClip = 0.3f, farClip = 200;
  Vector3 position, right, up, forward;  // world space, unit length (forward = the direction the camera looks)
};

// Screen size in the renderer's virtual pixels. renderWorld sets it every frame; Camera::ScreenToWorldPoint & co read it
// (480x272 before the first frame).
namespace RenderScreen {
void set(u32 width, u32 height);
u32 width();
u32 height();
}  // namespace RenderScreen

// The view of `cam` on a screen of screenW x screenH pixels. A default camera (full viewport) yields exactly the matrices the
// renderer always used: view = inverse(world matrix), perspective(fov, W / H, near, far).
CameraView makeCameraView(const Camera& cam, u32 screenW, u32 screenH);

// Screen <-> world with a CameraView. Screen points have the origin top-left, z = distance along the view direction.
Vector3 viewScreenToWorld(const CameraView& v, const Vector3& screenPoint);
Vector3 viewWorldToScreen(const CameraView& v, const Vector3& worldPoint);
Ray viewScreenRay(const CameraView& v, const Vector2& screenPoint);
// Viewport points: 0..1 of the camera's rectangle, origin bottom-left (Unity).
Vector3 viewWorldToViewport(const CameraView& v, const Vector3& worldPoint);
Vector3 viewViewportToWorld(const CameraView& v, const Vector3& viewportPoint);
Ray viewViewportRay(const CameraView& v, const Vector2& viewportPoint);
// World size that covers about `pixels` screen pixels at `point` (line widths that stay readable at any distance).
Scalar viewPixelSize(const CameraView& v, const Vector3& point, Scalar pixels);

// A layer is drawn when bit (layer & 31) of the camera's cullingMask is set.
inline bool layerInMask(i32 cullingMask, u32 layer) { return ((static_cast<u32>(cullingMask) >> (layer & 31u)) & 1u) != 0; }

// ---- particles ------------------------------------------------------------------------------------------------------------------
namespace Particles {
constexpr u32 kPoolCapacity = 2048;  // particles alive at once in the whole program (static pool, ~80 KB)
constexpr u32 kMaxSystems = 64;      // ParticleSystems with a pool block at once
// Advances every system by one fixed step (called by GameLoop::frame after the physics step). Systems with playOnAwake start here.
void fixedStep(World& world, Scalar dt);
void play(ParticleSystem& ps);
void stop(ParticleSystem& ps);
void emit(ParticleSystem& ps, i32 count);
void clear(ParticleSystem& ps);
// Draws every system's particles as camera-facing quads. `cullingMask` filters by the owner's layer.
void draw(World& world, IRenderer& renderer, const CameraView& view, i32 cullingMask);
// Frees every block (scene reload, tests). The pool is empty afterwards.
void reset();
u32 live();            // particles alive now, all systems
u32 reserved();        // pool entries held by blocks
u32 systems();         // blocks in use
u32 rejected();        // particles that could not be emitted because a pool was full, since reset()
u32 quadsDrawn();      // quads submitted by the last draw()
}  // namespace Particles

// ---- ribbons --------------------------------------------------------------------------------------------------------------------
namespace Ribbons {
// A line segment a -> b as one quad facing the camera, `width` world units wide. Writes the four corners.
void segmentQuad(const CameraView& v, const Vector3& a, const Vector3& b, Scalar width, Color32 color, Quad3D& out);
// Direction from the camera to `point` (the camera's forward when they coincide).
inline Vector3 safeViewDir(const CameraView& v, const Vector3& point) {
  const Vector3 d = point - v.position;
  return d.sqrMagnitude() > 1e-10f ? d.normalized() : v.forward;
}
// LineRenderers (their object's layer against `cullingMask`).
void drawLines(World& world, IRenderer& renderer, const CameraView& view, i32 cullingMask);
// Debug::DrawLine lines of this frame (drawn in dev builds; RENSGA_RELEASE builds and setDebugDraw(false) draw none).
void drawDebug(IRenderer& renderer, const CameraView& view);
u32 quadsDrawn();      // quads submitted by the last drawLines + drawDebug
void addQuads(u32 n);  // counter used by drawDebug
}  // namespace Ribbons

void setDebugDraw(bool enabled);
bool debugDrawEnabled();

// Submits `count` quads of texture `tex` to the renderer; when it cannot draw 3D quads (GS), projects them to screen rectangles.
// `texW` / `texH` are the texture size in pixels (the fallback samples the whole texture); 0 = untextured.
void submitQuads(IRenderer& renderer, const CameraView& view, u32 tex, u32 texW, u32 texH, const Quad3D* quads, u32 count);

}  // namespace rensga
