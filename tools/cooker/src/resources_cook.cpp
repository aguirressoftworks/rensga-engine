#include "resources_cook.h"

#include <algorithm>
#include <map>

#include "rensga/core/reflect.h"

namespace rensga {
namespace cooker {

namespace {
bool endsWith(const std::string& s, const char* suf) {
  const size_t n = std::char_traits<char>::length(suf);
  return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

struct KindRule {
  const char* suffix;
  PackResourceKind kind;
};
// Longest suffix first: ".mat.json" must win over ".json".
const KindRule kRules[] = {
    {".mat.json", PackResourceKind::Material},        {".font.json", PackResourceKind::Font},
    {".controller.json", PackResourceKind::AnimatorController}, {".clip.json", PackResourceKind::PropClip},
    {".asset.json", PackResourceKind::DataAsset},     {".prefab", PackResourceKind::Prefab},
    {".png", PackResourceKind::Texture},              {".wav", PackResourceKind::Audio},
    {".gltf", PackResourceKind::Mesh},                {".glb", PackResourceKind::Mesh},
};

bool classify(const std::string& rel, std::string& stem, PackResourceKind& kind) {
  for (const KindRule& r : kRules) {
    if (!endsWith(rel, r.suffix)) continue;
    stem = rel.substr(0, rel.size() - std::char_traits<char>::length(r.suffix));
    kind = r.kind;
    return true;
  }
  return false;
}

// The part of a path below the first "Resources/" folder, or empty when there is none.
bool belowResources(const std::string& stem, std::string& name) {
  const std::string key = "/Resources/";
  const std::string padded = "/" + stem;
  const size_t at = padded.find(key);
  if (at == std::string::npos) return false;
  name = padded.substr(at + key.size());
  return !name.empty();
}

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}
}  // namespace

bool buildResourceTable(const std::vector<ResourceAsset>& assetsIn, const std::function<u32(const std::string&)>& addStr,
                        std::vector<PackResource>& out, std::string& error) {
  out.clear();
  std::vector<ResourceAsset> assets = assetsIn;
  std::sort(assets.begin(), assets.end(), [](const ResourceAsset& a, const ResourceAsset& b) { return a.rel < b.rel; });
  std::map<std::string, std::string> seen;  // "kind|lower name" -> rel
  for (const ResourceAsset& a : assets) {
    std::string stem, name;
    PackResourceKind kind;
    if (!classify(a.rel, stem, kind) || !belowResources(stem, name)) continue;
    const std::string key = std::to_string(static_cast<unsigned>(kind)) + "|" + lower(name);
    auto it = seen.find(key);
    if (it != seen.end()) {
      error = "Resources path '" + name + "' is used by both '" + it->second + "' and '" + a.rel + "' (names are matched without case and extension)";
      return false;
    }
    seen[key] = a.rel;
    PackResource r = {};
    if (!Guid::parse(a.guid.c_str(), r.guid)) continue;
    r.nameHash = packResourceNameHash(name.c_str());
    r.nameStr = addStr(name);
    r.kind = static_cast<u32>(kind);
    out.push_back(r);
  }
  std::stable_sort(out.begin(), out.end(), [](const PackResource& a, const PackResource& b) { return a.nameHash < b.nameHash; });
  return true;
}

std::string markAdditiveScenes(const nlohmann::json& projectJson, const std::vector<std::string>& sceneGuids, std::vector<PackSceneRec>& scenes) {
  if (!projectJson.is_object() || !projectJson.contains("additiveScenes") || !projectJson["additiveScenes"].is_array()) return "";
  for (const nlohmann::json& g : projectJson["additiveScenes"]) {
    if (!g.is_string()) continue;
    const std::string want = g.get<std::string>();
    bool found = false;
    for (size_t i = 0; i < sceneGuids.size() && i < scenes.size(); ++i)
      if (sceneGuids[i] == want) {
        scenes[i].reserved |= kSceneAdditive;
        found = true;
      }
    if (!found) return want;
  }
  return "";
}

bool buildPoolReserve(const nlohmann::json& projectJson, std::vector<PackPoolReserve>& out, std::string& error) {
  out.clear();
  if (!projectJson.is_object() || !projectJson.contains("componentReserve")) return true;
  const nlohmann::json& r = projectJson["componentReserve"];
  if (!r.is_object()) {
    error = "ProjectSettings/Project.json: componentReserve must be an object {\"Type\": extraSlots}";
    return false;
  }
  for (auto it = r.begin(); it != r.end(); ++it) {  // nlohmann objects iterate in key order: the section is deterministic
    const TypeInfo* t = TypeRegistry::instance().find(it.key().c_str());
    if (!t || !t->asComponent) {
      error = "ProjectSettings/Project.json: componentReserve names '" + it.key() + "', which is not a component type";
      return false;
    }
    if (!it.value().is_number_integer() || it.value().get<long long>() < 1 || it.value().get<long long>() > 4096) {
      error = "ProjectSettings/Project.json: componentReserve." + it.key() + " must be a whole number from 1 to 4096";
      return false;
    }
    PackPoolReserve e = {t->hash, static_cast<u32>(it.value().get<long long>())};
    out.push_back(e);
  }
  return true;
}

}  // namespace cooker
}  // namespace rensga
