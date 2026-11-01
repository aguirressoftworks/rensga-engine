// F6.01 (Editor 6): prefab expansion, shared by the editor and the cooker. Pure JSON, no reflection and no ImGui.
//
// A scene stores the ROOT of every prefab instance (guid, name, parent, transform plus the "prefab" key and the modification
// lists, see docs/rensga-engine/prefabs.md). Expansion turns it into ordinary scene objects:
//   * members get deterministic GUIDs (instanceObjectGuid(rootGuid, fileId)), the root keeps its own;
//   * values are layered base < nested < variant < instance (each layer is applied on top of the previous one);
//   * the runtime never sees a prefab: the cooker expands before it reads the scene.
// The same function builds the editor's view of a scene, so what the Hierarchy shows is what the cooker packs.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/prefab_format.h"

namespace rensga {
namespace editor {

using PrefabLoader = std::function<bool(const std::string& guid, PrefabAsset& out, std::string& err)>;
using PrefabNamer = std::function<std::string(const std::string& guid)>;  // GUID -> readable name for error messages (optional)

// What the editor needs to know about one expanded object (an instance root, a member or an object added to an instance).
struct MemberComp {
  std::string type;
  int occurrence = 0;       // which component of that type on this object (0 = first)
  std::string addedId;      // non-empty: this component was added by the instance (its componentId); "" = comes from the prefab
};
struct MemberInfo {
  std::string rootGuid;     // scene GUID of the instance root this object belongs to
  FileId fileId;            // id inside the prefab ("" for an object added by the instance)
  bool isRoot = false;
  bool added = false;       // an object stored in the root's addedObjects (an ordinary scene entry; a nested instance root is also isRoot)
  std::string outerGuid;    // for an added nested instance root: the instance root whose addedObjects hold it
  std::vector<MemberComp> comps;   // parallel to the expanded object's "components"
};

struct ExpandOptions {
  PrefabNamer namer;
  std::vector<std::string>* warnings = nullptr;   // overrides whose target is gone, ...
};

// Expands one instance root (a scene-object JSON with the "prefab" key). Appends the root and all its members (pre-order,
// siblings in the prefab's order), then the objects added to the instance, to `out`. `members` (optional) receives an entry per
// appended object. False with `err` set when the prefab (or a nested one) is missing, cyclic or nested too deep.
bool expandInstance(const nlohmann::ordered_json& rootObject, const PrefabLoader& load, std::vector<nlohmann::ordered_json>& out,
                    std::map<std::string, MemberInfo>* members, std::string& err, const ExpandOptions& opt = ExpandOptions());

// Expands every instance in a scene's "objects" array in place order. Objects without "prefab" are copied as they are. An
// instance that fails to expand is appended as its plain root (no members) and its message goes to `errors`; the return value is
// true only when `errors` stays empty.
bool expandSceneObjects(const nlohmann::ordered_json& objects, const PrefabLoader& load, nlohmann::ordered_json& out,
                        std::map<std::string, MemberInfo>* members, std::vector<std::string>& errors,
                        const ExpandOptions& opt = ExpandOptions());

bool hasPrefabInstances(const nlohmann::json& objects);   // any object carries a non-null "prefab"

// Checks a prefab asset on its own: expands it (cycles, depth, missing nested or base assets, overrides to missing targets are
// warnings). Returns the problems, empty when the prefab is sound.
std::vector<std::string> checkPrefab(const std::string& guid, const PrefabLoader& load, const PrefabNamer& namer = PrefabNamer());

// The prefab as plain scene objects, for a runtime Instantiate template: the root is inactive and carries `rootGuid` as its GUID,
// members derive from it. Used by the cooker for prefabs referenced by a Guid field.
bool expandPrefabTemplate(const std::string& prefabGuid, const std::string& rootGuid, const PrefabLoader& load,
                          std::vector<nlohmann::ordered_json>& out, std::string& err);

// Value helpers shared with the editor.
// Splits "Type.field.2.x" into ("Type", "field.2.x"); false for "@..." paths and malformed ones.
bool splitOverridePath(const std::string& path, std::string& type, std::string& rest);
// Sets `rest` ("field", "field.x", "field.2", "field.2.x") inside a component's fields object.
void setFieldPath(nlohmann::ordered_json& fields, const std::string& rest, const nlohmann::json& value);
// Reads it back; null json when the field (or part) is absent.
nlohmann::json getFieldPath(const nlohmann::json& fields, const std::string& rest);

}  // namespace editor
}  // namespace rensga
