// F6.01: cook-time prefab handling (the runtime has no prefab concept). Called from cook.cpp once per scene, before the scene's
// objects are read through reflection.
#pragma once
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {

// GUID of the Instantiate template that a scene gets for a prefab (deterministic, so the scene cooks the same every time).
std::string prefabTemplateGuid(const std::string& sceneAssetGuid, const std::string& prefabGuid);

// 1. Expands every prefab instance of `scene["objects"]` into plain objects (members, overrides, nested prefabs, variants).
// 2. Every Guid field (or Guid array) that names a prefab asset is rewritten to the GUID of a template object that is appended to
//    the scene: the prefab expanded into inactive plain objects, so `Instantiate(GetObject(guid))` works at run time and the
//    script field keeps meaning "this prefab". Resources entries use the same template (appendPrefabTemplate).
// `prefabFiles` maps prefab asset GUIDs to files. Returns false with messages in `errors`; `notes` receives one line per
// expanded instance and template (for the cook log).
bool cookExpandPrefabs(nlohmann::json& scene, const std::string& sceneAssetGuid, const std::map<std::string, std::string>& prefabFiles,
                       std::vector<std::string>& errors, std::vector<std::string>& notes);

// Appends the template of one prefab to `scene["objects"]` unless the scene has it already; returns its root GUID ("" on error).
std::string appendPrefabTemplate(nlohmann::json& scene, const std::string& sceneAssetGuid, const std::string& prefabGuid,
                                 const std::map<std::string, std::string>& prefabFiles, std::vector<std::string>& errors);

}  // namespace rensga
