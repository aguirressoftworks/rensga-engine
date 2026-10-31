// F5 (PSP Engine 6): light gathering and per-object selection. No heap: the LightSet lives on the caller's stack.
#include "rensga/core/lighting.h"

#include "rensga/core/components.h"
#include "rensga/core/log.h"

namespace rensga {

void gatherLights(World& world, LightSet& out) {
  out.count = 0;
  out.ambient = Color32(0, 0, 0, 255);
  out.fogOn = false;
  out.fogColor = Color32(0, 0, 0, 255);
  out.fogStart = out.fogEnd = 0;
  static bool warned = false;
  bool dropped = false;
  bool anyLight = false;

  world.forEach<Light>([&](Light& l) {
    GameObject* go = l.gameObject();
    if (!go || !go->activeInHierarchy() || !l.enabled()) return;
    anyLight = true;
    if (out.count >= kMaxFrameLights) { dropped = true; return; }
    RenderLight& r = out.lights[out.count];
    r = RenderLight();
    r.color = l.color;
    r.intensity = l.intensity;
    if (l.type == LightType::Point) {
      r.type = 1;
      r.range = l.range;
      r.vec = go->transform.position();
    } else {
      r.type = 0;
      r.vec = go->transform.forward().normalized();
    }
    out.important[out.count] = l.renderMode == LightRenderMode::Important ? 1 : 0;
    ++out.count;
  });
  // Compatibility: a scene that only has the old baked-era DirectionalLight still gets that one light.
  if (!anyLight) {
    world.forEach<DirectionalLight>([&](DirectionalLight& d) {
      GameObject* go = d.gameObject();
      if (!go || !go->activeInHierarchy() || !d.enabled() || out.count >= kMaxFrameLights) return;
      RenderLight& r = out.lights[out.count];
      r = RenderLight();
      r.type = 0;
      r.color = d.color;
      r.intensity = 1;
      r.vec = d.direction.normalized();
      out.important[out.count] = 0;
      ++out.count;
    });
  }
  if (dropped && !warned) {
    warned = true;
    RLOG_WARN("render", "more than %u lights in the scene: the extra ones are ignored", static_cast<unsigned>(kMaxFrameLights));
  }

  bool haveSettings = false;
  world.forEach<RenderSettings>([&](RenderSettings& s) {
    GameObject* go = s.gameObject();
    if (haveSettings || !go || !go->activeInHierarchy() || !s.enabled()) return;
    haveSettings = true;
    out.ambient = s.ambient;
    out.fogOn = s.fogEnabled;
    out.fogColor = s.fogColor;
    out.fogStart = s.fogStart;
    out.fogEnd = s.fogEnd;
  });
}

namespace {
// Score of light `l` at the sphere: intensity x attenuation. Directional lights do not attenuate.
Scalar scoreOf(const RenderLight& l, const Vector3& c, Scalar radius) {
  if (l.intensity <= 0) return 0;
  if (l.type == 0) return l.intensity;
  if (l.range <= 0) return 0;
  Vector3 d = l.vec - c;
  Scalar dist = Mathf::Sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  Scalar over = dist - radius;
  if (over < 0) over = 0;
  Scalar a = 1.0f - over / l.range;
  if (a <= 0) return 0;
  return l.intensity * a * a;
}
}  // namespace

u32 selectLights(const LightSet& set, const Vector3& center, Scalar radius, u32 maxLights, u8* outIndices) {
  if (maxLights > kMaxDrawLights) maxLights = kMaxDrawLights;
  const u32 n = set.count < kMaxFrameLights ? set.count : kMaxFrameLights;
  Scalar score[kMaxFrameLights];
  bool forced[kMaxFrameLights];
  bool used[kMaxFrameLights];
  for (u32 i = 0; i < n; ++i) {
    forced[i] = set.lights[i].type == 0 || set.important[i];
    // Forced lights always qualify unless they are black; the others need a non-zero score.
    score[i] = forced[i] ? (set.lights[i].intensity > 0 ? 1.0f : 0.0f) : 0.0f;
    if (!forced[i]) score[i] = scoreOf(set.lights[i], center, radius);
    used[i] = score[i] <= 0;  // skipped
  }
  u32 got = 0;
  while (got < maxLights) {
    i32 best = -1;
    for (u32 i = 0; i < n; ++i) {
      if (used[i]) continue;
      if (best < 0) { best = static_cast<i32>(i); continue; }
      const u32 b = static_cast<u32>(best);
      bool better;
      if (forced[i] != forced[b]) better = forced[i];
      else if (forced[i]) better = false;                // forced: component order, lower index already best
      else better = score[i] > score[b];                 // ties keep the lower index
      if (better) best = static_cast<i32>(i);
    }
    if (best < 0) break;
    used[best] = true;
    outIndices[got++] = static_cast<u8>(best);
  }
  return got;
}

u32 resolveMaxLights(const PackConsoleTargetExt* ctg2, RuntimePlatform platform) {
  u32 n = (ctg2 && ctg2->maxLightsPerObject) ? ctg2->maxLightsPerObject : (platform == RuntimePlatform::Psp ? 4u : 8u);
  return n > kMaxDrawLights ? kMaxDrawLights : n;
}

}  // namespace rensga
