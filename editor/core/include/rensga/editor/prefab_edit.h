// F6.01 (Editor 6): editing operations on prefab data, independent of SceneDocument so they can be tested alone.
//   * creating a prefab from a subtree of scene objects,
//   * applying instance modifications to the prefab (base, variant or the nested entry that owns the target),
//   * converting a prefab to and from the "stage" scene used by prefab edit mode.
// No ImGui, no console names.
#pragma once
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/prefab_format.h"
#include "rensga/editor/prefab_library.h"

namespace rensga {
namespace editor {

// `subtree`: raw scene-object JSON, the root first and parents before children (what SceneDocument stores; members of other
// instances are never in it). Builds the prefab: file ids are allocated in that order (the root gets 1), component fields that name
// another object of the subtree become "@<fileId>" tokens. `idOf` receives scene GUID -> prefab FileId.
// A nested instance (an object with "prefab") becomes a nested entry; added objects on it are refused (apply or revert them first).
bool prefabFromSceneObjects(const std::vector<nlohmann::ordered_json>& subtree, const std::string& prefabGuid, PrefabAsset& out,
                            std::map<std::string, FileId>& idOf, std::string& err);

// A scene-object JSON that is the root of an instance of `prefab` (members are not stored).
nlohmann::ordered_json makeInstanceRoot(const std::string& guid, const std::string& name, const std::string& parentGuid,
                                        const PrefabAsset& prefab, const std::string& prefabGuid);

// Which parts of an instance's modifications to apply.
// Writes `mods` into `a`: a base prefab gets its objects and components changed (a nested entry's own mods for ids that reach into
// a nested prefab); a variant gets them in its modification lists. `guidToFileId` maps the scene GUIDs that added objects use as
// parents to prefab file ids. Appends one line per problem to `warnings` (targets that do not exist).
bool applyModsToPrefab(PrefabAsset& a, const PrefabModifications& mods, const std::map<std::string, FileId>& guidToFileId,
                       std::vector<std::string>& warnings);

// Removes from `all` what `part` holds (matching overrides by fileId/path/occurrence, components by their ids, removed objects
// and components by value, added objects by guid).
void subtractMods(PrefabModifications& all, const PrefabModifications& part);

// ---- prefab edit mode ----
// The stage scene: a normal scene JSON ({"format":1,"prefabStage":{...},"objects":[...]}) so the Hierarchy, Scene view and
// Inspector edit it unchanged. Objects and components keep their prefab ids in extra "fileId" keys (SceneDocument leaves unknown
// keys alone); scene GUIDs are derived from the ids, so selection survives a reload. A variant is shown as one instance of its
// base carrying the variant's modifications.
nlohmann::ordered_json prefabToStage(const PrefabAsset& a);
// Back to a prefab. Objects without "fileId" (created or duplicated in the stage) get new ids from nextFileId; duplicated ids are
// renumbered. False with `err` when the stage is not a valid prefab (not exactly one root, ...).
// `ids` (optional) keeps the ids given to objects that had none, by scene GUID, so an object created in the stage keeps the same
// prefab id across saves, undo and redo; `ids->next` is the next free id and only grows.
struct StageIds {
  std::map<std::string, FileId> objects;
  long long next = 1;
};
bool stageToPrefab(const nlohmann::ordered_json& stage, PrefabAsset& out, std::string& err, StageIds* ids = nullptr);
std::string stageObjectGuid(const std::string& prefabGuid, const FileId& id);

}  // namespace editor
}  // namespace rensga
