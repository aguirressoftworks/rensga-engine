// MVP-22: budget validator. Runs before anything reaches a console; errors name the asset and GUID.
#include <string>

#include "rensga/cooker/cooker.h"

namespace rensga {

bool validateBudgets(const ConsoleTargetData& t, const SceneBudgetInput& in, std::vector<CookMessage>& errors) {
  size_t before = errors.size();
  auto add = [&](const std::string& what) {
    CookMessage m;
    m.severity = CookMessage::Error;
    m.asset = in.sceneAsset;
    m.assetGuid = in.sceneGuid;
    m.text = "budget exceeded in '" + in.sceneAsset + "' (guid " + in.sceneGuid + "): " + what;
    errors.push_back(m);
  };
  if (t.ramLimitBytes && t.ramBytes > t.ramLimitBytes)
    add("ConsoleTarget budgets.ramBytes is " + std::to_string(t.ramBytes) + ", above the " + t.profile + " device limit of " +
        std::to_string(t.ramLimitBytes) + " bytes (PSP-1000 RAM)");
  if (t.ramBytes && in.packBytes > t.ramBytes)
    add("cooked pack is " + std::to_string(in.packBytes) + " bytes, RAM budget is " + std::to_string(t.ramBytes));
  if (t.vramBytes && in.textureBytes > t.vramBytes)
    add("textures are " + std::to_string(in.textureBytes) + " bytes, VRAM budget is " + std::to_string(t.vramBytes));
  if (t.trianglesPerFrame && in.triangles > t.trianglesPerFrame) {
    std::string s = std::to_string(in.triangles) + " triangles, budget is " + std::to_string(t.trianglesPerFrame);
    if (!in.heaviestObjectGuid.empty())
      s += "; heaviest object is '" + in.heaviestObjectName + "' (guid " + in.heaviestObjectGuid + ") with " +
           std::to_string(in.heaviestTriangles);
    add(s);
  }
  if (t.maxObjects && in.objects > t.maxObjects)
    add(std::to_string(in.objects) + " objects, budget is " + std::to_string(t.maxObjects));
  for (const auto& c : in.componentsPerType)
    if (t.maxComponentsPerType && c.count > t.maxComponentsPerType)
      add(std::to_string(c.count) + " '" + c.type + "' components, capacity per type is " +
          std::to_string(t.maxComponentsPerType));
  // F5.12: components that need a profile feature (the validator side of RENSGA_REQUIRES_FEATURE).
  struct Need { const char* type; const char* feature; const char* alt; const char* hint; };
  static const Need needs[] = {
      {"Light", "lighting", nullptr, "bake the light with a DirectionalLight, or add \"lighting\" to ConsoleTarget.json features"},
      {"SkinnedMeshRenderer", "skinning", nullptr, "use a MeshRenderer with sprite or vertex animation, or add \"skinning\" to ConsoleTarget.json features"},
      {"Animation", "skinning", nullptr, "use sprite or vertex animation, or add \"skinning\" to ConsoleTarget.json features"},
      {"RenderSettings", "lighting", "fog", "remove it, or add \"lighting\" or \"fog\" to ConsoleTarget.json features"},
  };
  for (const auto& c : in.componentsPerType)
    for (const Need& n : needs)
      if (c.type == n.type && c.count > 0 && !t.hasFeature(n.feature) && !(n.alt && t.hasFeature(n.alt)))
        add(std::to_string(c.count) + " '" + c.type + "' components, but the " + t.profile + " profile has no '" + n.feature +
            "' feature; " + n.hint);
  return errors.size() == before;
}

bool warnAudioVoices(const ConsoleTargetData& t, const SceneBudgetInput& in, std::vector<CookMessage>& warnings) {
  const uint64_t voices = t.audioVoices ? t.audioVoices : 16;
  if (in.audioStartingSources <= voices) return true;
  CookMessage m;
  m.severity = CookMessage::Warning;
  m.asset = in.sceneAsset;
  m.assetGuid = in.sceneGuid;
  m.text = "audio voices in '" + in.sceneAsset + "' (guid " + in.sceneGuid + "): " + std::to_string(in.audioStartingSources) +
           " AudioSources start with the scene (looping or play on awake), the " + t.profile + " target has " + std::to_string(voices) +
           " voices (ConsoleTarget audio.voices); the least important (highest priority number) will be stolen. Lower the count or "
           "set AudioSource.priority";
  warnings.push_back(m);
  return false;
}

}  // namespace rensga
