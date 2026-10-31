// Prefab file format (F6.01, contract E0): data structs plus JSON load/save. No behaviour (instantiate, apply, revert,
// merge and the cook-time expansion are A1's). Spec with examples and the precedence rules: docs/rensga-engine/prefabs.md.
#pragma once
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

constexpr int kPrefabFormat = 1;
constexpr int kPrefabMaxNesting = 8;  // validator limit for nested prefabs / variant chains

// A stable per-object (or per-component) id inside one prefab file. A plain id is a decimal integer ("7"); an id that
// reaches into a nested prefab is the path of ids from the outer prefab, joined with '/' ("3/7" = object 7 of the nested
// instance whose root entry is object 3 here). On disk plain ids are JSON numbers, paths are strings. Ids are never reused
// (PrefabAsset::nextFileId only grows), so overrides survive reordering and renaming.
using FileId = std::string;
FileId fileIdFromJson(const nlohmann::json& j);   // number or string -> canonical text; "" when neither
nlohmann::json fileIdToJson(const FileId& id);    // all digits -> number, else string

// "Type.field", "Type.field.x" (Vector3/Quaternion part), "Type.field.2", "Type.field.2.x" (Array element): the scene-level
// form of reflect.h's FieldPath, with the component type prepended. Object-level properties use "@name", "@active",
// "@tag", "@layer", "@position", "@rotation", "@scale" (the root object's @position/@rotation/@scale are never overrides:
// every instance has its own placement and the scene object's transform is authoritative).
struct PrefabOverride {
  FileId fileId;           // the object (in the prefab the instance came from)
  std::string path;
  int occurrence = 0;      // which component of that type when an object has several (0 = first); ignored for "@" paths
  nlohmann::json value;
};

struct PrefabComponent {
  FileId fileId;           // stable id of this component inside the prefab
  std::string type;
  nlohmann::json fields = nlohmann::json::object();
};

struct PrefabAddedComponent {  // a component added to a member object by an instance, variant or nested reference
  FileId fileId;               // the object it was added to
  PrefabComponent component;   // its own fileId is unique among the owner's added components (allocated by the owner)
};

struct PrefabRemovedComponent {  // a component of the base removed on one member object
  FileId fileId;                 // the object
  std::string type;
  int occurrence = 0;
};

// Everything one reference to a prefab changes: the body of a scene instance, of a nested prefab entry and of a variant.
struct PrefabModifications {
  std::vector<PrefabOverride> overrides;
  std::vector<PrefabAddedComponent> addedComponents;
  std::vector<PrefabRemovedComponent> removedComponents;
  std::vector<FileId> removedObjects;       // base objects deleted (their subtree goes too)
  nlohmann::json addedObjects = nlohmann::json::array();  // object entries in the owner's own format (see prefabs.md), parent = a base FileId or "" for the root
  bool empty() const { return overrides.empty() && addedComponents.empty() && removedComponents.empty() && removedObjects.empty() && addedObjects.empty(); }
};

struct PrefabObject {
  FileId fileId;
  std::string name;
  FileId parent;                   // "" = the prefab root
  bool active = true;
  std::string tag;
  int layer = 0;
  float position[3] = {0, 0, 0};
  float rotation[4] = {0, 0, 0, 1};
  float scale[3] = {1, 1, 1};
  std::vector<PrefabComponent> components;
  // Nested prefab: when `nestedAsset` is set this entry is the root of an instance of that prefab and carries no own
  // components or children of the nested prefab (those come from the asset); its own components/children may still be added
  // through `nested.addedComponents/addedObjects`.
  std::string nestedAsset;         // GUID of the nested prefab, "" for a plain object
  FileId nestedRootFileId;         // that prefab's rootFileId when the entry was created
  PrefabModifications nested;
};

// <name>.prefab. Either `objects` (a base prefab, root = rootFileId) or `variantBase` (a variant, no objects of its own).
struct PrefabAsset {
  std::string guid;                // the asset GUID (same value as in <name>.prefab.meta)
  FileId rootFileId;
  long long nextFileId = 1;        // next free plain id
  std::vector<PrefabObject> objects;
  std::string variantBase;         // GUID of the base prefab; non-empty = this file is a variant
  PrefabModifications variant;     // the variant's changes on top of the base

  bool isVariant() const { return !variantBase.empty(); }
  FileId allocFileId() { return std::to_string(nextFileId++); }
  const PrefabObject* find(const FileId& id) const;
};

// What a scene object that is the root of a prefab instance adds to the ordinary scene-object keys. The members of the
// instance are NOT stored in the scene: they are derived (instanceObjectGuid) and expanded by the editor and the cooker.
struct PrefabInstance {
  std::string asset;               // GUID of the prefab
  FileId fileId;                   // the prefab's rootFileId when the instance was made
  PrefabModifications mods;
};

// Scene-object JSON <-> PrefabInstance. toSceneJson writes the keys "prefab", "overrides", "addedComponents",
// "removedComponents", "removedObjects", "addedObjects" (only the non-empty ones) into an object that already holds
// guid/name/parent/active/transform. readSceneObject returns false (and leaves `out` untouched) when "prefab" is null/absent.
void writeInstanceKeys(nlohmann::ordered_json& sceneObject, const PrefabInstance& inst);
bool readInstanceKeys(const nlohmann::json& sceneObject, PrefabInstance& out, std::string& err);

nlohmann::ordered_json toJson(const PrefabAsset& p);
bool fromJson(const nlohmann::json& j, PrefabAsset& out, std::string& err);
bool loadPrefab(const std::string& path, PrefabAsset& out, std::string& err);
bool savePrefab(const std::string& path, const PrefabAsset& p, std::string& err);

// Structural problems of one file (duplicate ids, missing parent, no root, variant with objects, bad nested id, a
// path that is neither "@x" nor "Type.field...", etc.), one message each. Cross-file checks (cycles, depth, missing
// assets) need the asset database and are A1's validator; kPrefabMaxNesting is the shared limit.
std::vector<std::string> validate(const PrefabAsset& p);

// Deterministic scene GUID of a member of an instance: a 128-bit hash (version 5 style UUID text) of the instance root's
// scene GUID and the member's FileId path. The root of the instance keeps its own scene GUID (member FileId == the
// prefab's rootFileId maps to rootGuid).
std::string instanceObjectGuid(const std::string& instanceRootGuid, const FileId& memberFileId);

}  // namespace editor
}  // namespace rensga
