# Rendering API (F6.09, API 7)

Cameras, per-instance material changes, particles and lines. Code: `engine/core/include/rensga/core/render_view.h`, `engine/core/src/{render_view,particles,line_renderer,debug_draw}.cpp`, `renderWorld` in `engine/core/src/loop.cpp`, the Camera and ParticleSystem methods in `engine/api/src/render_api.cpp`. Sample: `samples/explosions` (`tools/ci/explosions.sh`).

## Cameras

Every enabled, active `Camera` draws, in increasing `depth` (ties: creation order), into its viewport rectangle.

| Field | Meaning |
| --- | --- |
| `orthographic`, `orthographicSize` | half of the vertical view size in world units; the width follows the viewport's aspect |
| `viewportX/Y/W/H` | Unity's `Camera.rect`: 0..1 of the screen, origin bottom-left |
| `depth` | draw order; `Camera::main()` is the enabled one with the lowest depth |
| `clearFlags` | Skybox and SolidColor clear color and depth in the rectangle, DepthOnly clears depth, Nothing clears nothing. The first camera is cleared with the frame |
| `backgroundColor` | SolidColor clear. The default (40, 60, 90) keeps the color the player passes to `renderWorld` |
| `cullingMask` | bit n set = layer n is drawn (meshes, particles, lines) |

`Camera::ScreenToWorldPoint`, `WorldToScreenPoint`, `ScreenPointToRay`, `WorldToViewportPoint`, `ViewportToWorldPoint` and `ViewportPointToRay` use the same `makeCameraView` as the renderer. Screen points are virtual pixels with the origin top-left (the 2D and UI space); `z` is the distance along the view direction. A scene with one default camera makes the same renderer calls as before F6. The camera looks down its local -Z (view = inverse of the world matrix).

`Input::mousePresent()`, `mousePosition()`, `GetMouseButton(0)`, `GetMouseButtonDown(0)` and `GetMouseButtonUp(0)` give the pointer (mouse, or the first finger) in the same virtual pixels. PSP and PS2 have none: use the nub and a button.

Renderer seam (`IRenderer`): `setViewport`, `clearView`, `drawQuads3D`; all default to "not supported". GL and GU implement them. The GU records the frame and draws it at `endFrame`, so each camera is a pass (at most 6 per frame, extra cameras merge into the last) with its own matrices, GE viewport, scissor and clear. The GS has no 3D: `submitQuads` turns axis-aligned quads (particles seen by an orthographic camera) into `drawSprite` / `drawRect` calls and skips rotated ones (line ribbons).

## Per-instance material changes

`MeshRenderer::SetPropertyBlock(MaterialPropertyBlock)` (also `SkinnedMeshRenderer`): `SetColor` multiplies into the material color, `SetTexture` replaces the texture, the material stays shared. It travels in `DrawParams` (matColor, texture), so GL and GU honour it for meshes cooked with a material (the lit path). A mesh without a material is baked at cook time (vertex colors) and has nothing to override. The GS has no 3D meshes.

## ParticleSystem

CPU, world space, simulated once per fixed step (`Particles::fixedStep`, called by `GameLoop::frame`). One static pool of `Particles::kPoolCapacity` (2048) particles for the whole program, cut into one block of `maxParticles` (1..512) per system, at most 64 systems; no heap, no per-frame allocation. A system that finds no room cannot emit (a warning is logged once, `Particles::rejected()` counts the particles that did not fit).

| Field | Behaviour |
| --- | --- |
| `playOnAwake`, `duration`, `loop` | a system starts on its first fixed step; without `loop` it stops emitting after `duration` and the living particles finish |
| `emissionRate`, `burstTimes` / `burstCounts` | particles per second, plus bursts at times (seconds from the start of a loop) |
| `shape` | Cone (base disc `shapeRadius`, opening `shapeAngle`, along the object's +Y), Sphere (inside, outward), Hemisphere (upper half), Box (half extent `shapeRadius`, up), Circle (XZ disc, outward), Point (all directions) |
| `startLifetime`, `startSpeed`, `startSize`, `startColor` | constant per system; the shape gives the random part |
| `gravityModifier` | times the 3D physics gravity |
| `colorOverLifetime` + `colorStart` / `colorEnd` | multiplied into `startColor` |
| `sizeOverLifetime` + `sizeStart` / `sizeEnd` | multipliers of `startSize` at birth and death |
| `seed` | private xorshift stream: the same scene gives the same particles |

Drawn as camera-facing quads (`Quad3D`), sorted back to front inside a system, alpha blended, depth tested, not depth written, after the opaque meshes of each camera. `texture` is a texture asset (a soft round sprite looks best); none = flat squares. Budget: two triangles per particle (the profiler counts them). Particles are visual only: nothing in the simulation reads them, so the determinism hashes do not change.

## LineRenderer and Debug::DrawLine

`LineRenderer`: up to 16 points, `width` in world units (never thinner than one pixel), `color`, `loop`, `useWorldSpace`. Each segment is one camera-facing quad. `Debug::DrawLine` lines are drawn the same way, about 1.5 pixels wide, in dev builds (`RENSGA_RELEASE` builds and `setDebugDraw(false)` draw none).

## Point-light falloff (one curve on GL and GU)

`lighting.h`: `pointLightFalloff(d, range) = 1 / (1 + kPointLightK (d / range)^2)` with `kPointLightK = 15`: 1 at the light, 0.5 at 0.26 of the range, 0.25 at 0.45, 1/16 at `range`. The GU sets `sceGuLightAtt(1, 0, K / range^2)`, which is the form the GE evaluates (1 / (c + l d + q d^2)); the desktop shader uses the same formula (`uPointK`) and, like the GE, clamps the lit vertex color to 0..1 before the texture multiplies it. `selectLights` still leaves out lights farther than `range` plus the object's radius. The cooker's baked vertex lighting (`vertex_bake.h`) still uses the older `clamp01(1 - d / range)^2`.

## Big meshes near the camera on the PSP

The GE drops a triangle with a vertex behind the eye or far outside its drawing area. `GuRenderer` clips such draws on the CPU against the near plane and the four sides (2.5x the screen) before drawing: lit meshes flagged `kMeshNeedsNearClip`, and now also every lit or baked mesh whose bounding sphere, scaled by the object's world matrix, is larger than 6 units (a unit cube used as a 30 x 40 floor). Baked meshes get their sphere when the pack is handed over (`setMeshes`). Each clipped draw owns a slice of a per-frame buffer (4096 vertices) until the display list has run.
