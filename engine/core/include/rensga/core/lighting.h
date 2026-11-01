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

// F6.09: the one shading curve of a point light, on every renderer. The PSP GE can only attenuate by 1 / (c + l d + q d^2), so the
// desktop shader uses the same: intensity / (1 + kPointLightK * (d / range)^2). That is 1 at the light, 0.5 at 0.26 of the range, 0.25
// at 0.45 and 1/16 (6 %) at `range`, where the older clamp01(1 - d / range)^2 reached 0; lights farther than range + radius are still
// left out by selectLights. GL: uPointK in the lit shader; GU: sceGuLightAtt(1, 0, kPointLightK / range^2).
constexpr Scalar kPointLightK = 15.0f;
inline Scalar pointLightFalloff(Scalar dist, Scalar range) {
  if (range <= 0) return 0;
  const Scalar x = dist / range;
  return 1.0f / (1.0f + kPointLightK * x * x);
}

// Picks the strongest lights for a sphere (world-space center and radius) and writes their LightSet indices to
// outIndices, strongest first. Returns how many (at most maxLights, at most 8).
// Order, exactly: 1) directional lights and renderMode == Important lights first (among them component order),
// 2) then by intensity x attenuation at the sphere (point light: intensity * clamp01(1 - max(0, dist - radius) / range)^2,
// highest first), 3) ties by component order (lower index first). Lights whose score is 0 are skipped.
u32 selectLights(const LightSet& set, const Vector3& center, Scalar radius, u32 maxLights, u8* outIndices);

// The per-object light limit: the CTG2 value when present and non-zero, else 4 on the PSP profile, else 8.
u32 resolveMaxLights(const PackConsoleTargetExt* ctg2, RuntimePlatform platform);

}  // namespace rensga
