// F5 (PSP Engine 6): per-object light selection. The renderer gets the frame's lights once (IRenderer::setFrameLights) and
// each draw names the strongest N through DrawParams::lights. Implemented in engine/core/src/lighting.cpp.
#pragma once
#include "rensga/core/application.h"
#include "rensga/core/backends.h"
#include "rensga/core/world.h"

namespace rensga {

// Light::renderMode values (Unity: Auto, Important, NotImportant).
namespace LightRenderMode {
constexpr i32 Auto = 0, Important = 1, NotImportant = 2;
}
namespace LightType {
constexpr i32 Directional = 0, Point = 1;
}

// The lights of one frame, in component (pool slot) order, plus the scene's RenderSettings.
struct LightSet {
  RenderLight lights[kMaxFrameLights];
  u8 important[kMaxFrameLights] = {};  // 1 = renderMode Important
  u32 count = 0;                       // lights beyond kMaxFrameLights are dropped (and logged once)
  Color32 ambient = Color32(0, 0, 0, 255);
  bool fogOn = false;
  Color32 fogColor = Color32(0, 0, 0, 255);
  Scalar fogStart = 0, fogEnd = 0;
};

// Fills `out` from the active, enabled Light components and the first active RenderSettings. Without any RenderSettings
// ambient is black and fog is off. No heap.
void gatherLights(World& world, LightSet& out);

// Picks the strongest lights for a sphere (world-space center and radius) and writes their LightSet indices to
// outIndices, strongest first. Returns how many (at most maxLights, at most 8).
// Order, exactly: 1) directional lights and renderMode == Important lights first (among them component order),
// 2) then by intensity x attenuation at the sphere (point light: intensity * clamp01(1 - max(0, dist - radius) / range)^2,
// highest first), 3) ties by component order (lower index first). Lights whose score is 0 are skipped.
u32 selectLights(const LightSet& set, const Vector3& center, Scalar radius, u32 maxLights, u8* outIndices);

// The per-object light limit: the CTG2 value when present and non-zero, else 4 on the PSP profile, else 8.
u32 resolveMaxLights(const PackConsoleTargetExt* ctg2, RuntimePlatform platform);

}  // namespace rensga
