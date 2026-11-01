#include "prefab_cook.h"

#include <functional>
#include <set>

#include "rensga/cooker/prefab_expand.h"
#include "rensga/core/reflect.h"

namespace rensga {

using nlohmann::json;
using nlohmann::ordered_json;
using namespace rensga::editor;

namespace {

PrefabLoader fileLoader(const std::map<std::string, std::string>& files) {
  return [&files](const std::string& guid, PrefabAsset& out, std::string& err) {
    auto it = files.find(guid);
    if (it == files.end()) { err = "no prefab asset has GUID " + guid; return false; }
    return loadPrefab(it->second, out, err);
  };
}

PrefabNamer fileNamer(const std::map<std::string, std::string>& files) {
  return [&files](const std::string& guid) {
    auto it = files.find(guid);
    if (it == files.end()) return guid;
    const size_t slash = it->second.find_last_of("/\\");
    return it->second.substr(slash == std::string::npos ? 0 : slash + 1);
  };
}

// The Guid-typed fields of a component type, by name (plain and array).
void collectGuidRefs(json& fields, const TypeInfo& ti, const std::function<void(json&)>& visit) {
  for (u32 f = 0; f < ti.fieldCount; ++f) {
    const FieldDesc& fd = ti.fields[f];
    const bool arr = fd.type == FieldType::Array && fd.elemType == FieldType::Guid;
    if (fd.type != FieldType::Guid && !arr) continue;
    if (!fields.is_object() || !fields.contains(fd.name)) continue;
    json& v = fields[fd.name];
    if (arr && v.is_array()) { for (json& e : v) visit(e); }
    else visit(v);
  }
}

}  // namespace

std::string prefabTemplateGuid(const std::string& sceneAssetGuid, const std::string& prefabGuid) {
  return instanceObjectGuid(sceneAssetGuid, "template/" + prefabGuid);
}

std::string appendPrefabTemplate(json& scene, const std::string& sceneAssetGuid, const std::string& prefabGuid,
                                 const std::map<std::string, std::string>& files, std::vector<std::string>& errors) {
  const std::string root = prefabTemplateGuid(sceneAssetGuid, prefabGuid);
  json& objs = scene["objects"];
  for (const json& o : objs) if (o.value("guid", std::string()) == root) return root;
  std::vector<ordered_json> made;
  std::string err;
  const PrefabLoader load = fileLoader(files);
  if (!expandPrefabTemplate(prefabGuid, root, load, made, err)) {
    errors.push_back("prefab template " + fileNamer(files)(prefabGuid) + ": " + err);
    return std::string();
  }
  for (ordered_json& m : made) objs.push_back(json::parse(m.dump()));
  return root;
}

bool cookExpandPrefabs(json& scene, const std::string& sceneAssetGuid, const std::map<std::string, std::string>& files,
                       std::vector<std::string>& errors, std::vector<std::string>& notes) {
  if (!scene.contains("objects") || !scene["objects"].is_array()) return true;
  const size_t errorsBefore = errors.size();
  const PrefabLoader load = fileLoader(files);
  ExpandOptions opt;
  opt.namer = fileNamer(files);
  std::vector<std::string> warnings;
  opt.warnings = &warnings;

  if (hasPrefabInstances(scene["objects"])) {
    int instances = 0;
    for (const json& o : scene["objects"]) if (o.contains("prefab") && !o["prefab"].is_null()) ++instances;
    ordered_json expanded;
    std::vector<std::string> errs;
    expandSceneObjects(ordered_json::parse(scene["objects"].dump()), load, expanded, nullptr, errs, opt);
    for (const std::string& e : errs) errors.push_back(e);
    if (errs.empty()) {
      scene["objects"] = json::parse(expanded.dump());
      notes.push_back("cook: expanded " + std::to_string(instances) + " prefab instance(s)");
    }
    for (const std::string& w : warnings) notes.push_back("cook: warning: " + w);
  }
  if (errors.size() != errorsBefore) return false;

  // Guid fields that name a prefab asset become references to an Instantiate template in this scene. A template can itself hold
  // such fields (a spawner prefab), so this repeats until nothing is left; each prefab gets one template.
  if (!files.empty()) {
    std::set<std::string> done;
    for (int round = 0; round < 16; ++round) {
      std::set<std::string> wanted;
      auto scan = [&](const std::function<void(json&)>& visit) {
        for (json& o : scene["objects"]) {
          if (!o.contains("components") || !o["components"].is_array()) continue;
          for (json& c : o["components"]) {
            const TypeInfo* ti = TypeRegistry::instance().find(c.value("type", std::string()).c_str());
            if (!ti || !c.contains("fields")) continue;
            collectGuidRefs(c["fields"], *ti, visit);
          }
        }
      };
      scan([&](json& v) { if (v.is_string() && files.count(v.get<std::string>())) wanted.insert(v.get<std::string>()); });
      if (wanted.empty()) break;
      for (const std::string& pg : wanted) {
        if (done.insert(pg).second) {
          if (appendPrefabTemplate(scene, sceneAssetGuid, pg, files, errors).empty()) continue;
          notes.push_back("cook: prefab template " + fileNamer(files)(pg));
        }
      }
      if (errors.size() != errorsBefore) return false;
      scan([&](json& v) {
        if (v.is_string() && wanted.count(v.get<std::string>())) v = prefabTemplateGuid(sceneAssetGuid, v.get<std::string>());
      });
    }
  }
  return errors.size() == errorsBefore;
}

}  // namespace rensga
