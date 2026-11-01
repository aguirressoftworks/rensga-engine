// F6.01 / gate G1 (A1): the "prefab override" column of the component matrix (tests/fixtures/component_matrix.h).
// For every Component type: a prefab whose root carries the type with scrambled values, two instances in a scene, one of them
// overrides ONE field (set through the editor's SceneDocument, so it is recorded, undoable and saved like any inspector edit).
// The scene is cooked for linux and psp and the cooked blobs are checked: the overridden instance holds the override and every other
// field still has the prefab's value, the second instance has the prefab's values everywhere. Then apply writes the override into the
// prefab file, and revert (after an undo) drops it again.
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <set>

#include "fixtures/component_matrix.h"
#include "rensga/cooker/cooker.h"
#include "rensga/core/pack_format.h"
#include "rensga/editor/asset_database.h"
#include "rensga/editor/prefab_edit.h"
#include "rensga/editor/prefab_library.h"
#include "rensga/editor/scene_document.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* kSceneGuid = "00000000-0000-4000-8000-00000000d0f2";
const char* kPrefabGuid = "00000000-0000-4000-8000-00000000d0f3";

// Fields that name an object of the same prefab (written as "@1", the root, which the expansion turns into the instance's GUID).
bool isObjectRef(const char* type, const char* field) {
  static const std::set<std::string> refs = {
      "Camera2D.target",     "Slider.fillRect",       "Slider.handleRect",      "Toggle.graphic",
      "EventSystem.firstSelected", "UIButton.selectOnUp", "UIButton.selectOnDown", "UIButton.selectOnLeft", "UIButton.selectOnRight"};
  return refs.count(std::string(type) + "." + field) != 0;
}

const char* assetGuid(const char* type, const char* field) {
  const std::string k = std::string(type) + "." + field;
  if (k == "MeshRenderer.mesh") return "00000000-0000-4000-8000-00000000c001";
  if (k == "MeshRenderer.material" || k == "SkinnedMeshRenderer.material") return "00000000-0000-4000-8000-000000000505";
  if (k == "SkinnedMeshRenderer.mesh" || k == "Animation.clip") return "00000000-0000-4000-8000-000000000501";
  return nullptr;
}

void copyRoomAssets(const fs::path& dir) {
  const fs::path room = fs::path(RENSGA_ROOM_DIR) / "Assets";
  for (const char* rel : {"Primitives/cube.prim.json", "Models/blocky_walk.gltf", "Textures/blocky_walk.png", "Textures/room.png", "Materials/character.mat.json"}) {
    fs::create_directories((dir / "Assets" / rel).parent_path());
    fs::copy_file(room / rel, dir / "Assets" / rel, fs::copy_options::overwrite_existing);
    fs::copy_file(room / (std::string(rel) + ".meta"), dir / "Assets" / (std::string(rel) + ".meta"), fs::copy_options::overwrite_existing);
  }
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void spit(const fs::path& p, const std::string& t) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << t;
}

// Object-reference tokens must be strings; the scrambled object holds a Guid, so write the field JSON by hand for those.
json fieldsWithRefs(const TypeInfo& t, const void* obj) {
  json j = fieldsToJson(t, obj);
  for (u32 i = 0; i < t.fieldCount; ++i)
    if (isObjectRef(t.name, t.fields[i].name)) j[t.fields[i].name] = "@1";
  return j;
}

struct Found { bool ok = false; std::vector<u8> blob; };

// The cooked blobs of type `t`, in object order.
std::vector<Found> cookedBlobs(const fs::path& project, const char* target, const TypeInfo& t, std::string* why) {
  CookOptions o;
  o.project = project;
  o.target = target;
  o.force = true;
  std::string firstError;
  CookResult r = cookProject(o, [&](const CookMessage& m) { if (m.severity == CookMessage::Error && firstError.empty()) firstError = m.text; });
  std::vector<Found> out;
  if (!r.ok) { *why = std::string("cook for ") + target + " failed: " + firstError; return out; }
  std::ifstream in(r.packPath, std::ios::binary);
  std::vector<u8> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  u8* mem = static_cast<u8*>(aligned_alloc(16, (bytes.size() + 15) & ~size_t(15)));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  if (!view.open(mem, bytes.size(), "prefab matrix")) { *why = std::string("the ") + target + " pack does not open"; free(mem); return out; }
  u32 n = 0;
  const PackComponent* pc = view.records<PackComponent>(pack_section::COMP, &n);
  const u8* fdat = view.section(pack_section::FDAT);
  std::vector<std::pair<u32, Found>> byObject;
  for (u32 i = 0; i < n; ++i) {
    if (pc[i].typeHash != t.hash) continue;
    Found f;
    f.ok = true;
    f.blob.assign(fdat + pc[i].fieldOffset, fdat + pc[i].fieldOffset + pc[i].fieldSize);
    byObject.push_back({pc[i].objectIndex, std::move(f)});
  }
  free(mem);
  std::sort(byObject.begin(), byObject.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (auto& p : byObject) out.push_back(std::move(p.second));
  return out;
}

bool prefabStep(const TypeInfo& t, const void* /*scrambled*/, std::string* why) {
  const fs::path dir = fs::temp_directory_path() / ("rensga_matrix_prefab_" + std::string(t.name));
  fs::remove_all(dir);
  fs::create_directories(dir / "Assets/Prefabs");
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok" / "ProjectSettings", dir / "ProjectSettings", fs::copy_options::recursive);
  if (strcmp(t.name, "MeshRenderer") == 0 || strcmp(t.name, "SkinnedMeshRenderer") == 0 || strcmp(t.name, "Animation") == 0) copyRoomAssets(dir);

  // ConsoleTarget: the type under test plus what it requires.
  json ct = json::parse(slurp(dir / "ProjectSettings/ConsoleTarget.json"));
  auto allow = [&](const char* name) {
    for (const auto& n : ct["allowedComponents"]) if (n == name) return;
    ct["allowedComponents"].push_back(name);
  };
  allow(t.name);
  std::vector<std::string> requiredTypes;
  for (u32 i = 0; i < t.requireCount; ++i) {
    const TypeInfo* rt = TypeRegistry::instance().find(t.requireNames[i]);
    if (!rt) continue;
    allow(rt->name);
    requiredTypes.push_back(rt->name);
  }
  if (strcmp(t.name, "Animation") == 0) { allow("SkinnedMeshRenderer"); requiredTypes.push_back("SkinnedMeshRenderer"); }
  ct["features"] = json::array({"lighting", "skinning", "fog"});
  spit(dir / "ProjectSettings/ConsoleTarget.json", ct.dump(1));
  json pj = json::parse(slurp(dir / "ProjectSettings/Project.json"));
  pj["scenes"] = json::array({kSceneGuid});
  spit(dir / "ProjectSettings/Project.json", pj.dump(1));

  // Prefab values (seed 3, as the cook step) and an override value from another scramble.
  matrix::Obj a(t), b(t);
  auto guidFor = [&](const FieldDesc& f) {
    Guid g;
    if (const char* ag = assetGuid(t.name, f.name)) Guid::parse(ag, g);
    return g;  // object refs are tokens in the JSON below; everything else the type has no asset for stays null
  };
  matrix::scramble(t, a.p(), 3, guidFor);
  matrix::scramble(t, b.p(), 5, guidFor);
  for (u32 i = 0; i < t.fieldCount; ++i)  // scramble flips every bool the same way: make the override value differ
    if (t.fields[i].type == FieldType::Bool) *reinterpret_cast<bool*>(static_cast<u8*>(b.p()) + t.fields[i].offset) = !*reinterpret_cast<bool*>(static_cast<u8*>(a.p()) + t.fields[i].offset);
  const json prefabFields = fieldsWithRefs(t, a.p());
  json otherFields = fieldsWithRefs(t, b.p());
  // The field to override: the first non-reference field whose scrambled value differs between the two seeds; a type that has
  // only asset references (SkinnedMeshRenderer) overrides one of them with "none".
  std::string field;
  for (u32 i = 0; i < t.fieldCount && field.empty(); ++i) {
    const FieldDesc& f = t.fields[i];
    if (f.type == FieldType::Guid || f.type == FieldType::UnityEvent || (f.type == FieldType::Array && f.elemType == FieldType::Guid)) continue;
    if (prefabFields[f.name] != otherFields[f.name]) field = f.name;
  }
  if (field.empty()) {
    matrix::Obj none(t);
    const json noneFields = fieldsToJson(t, none.p());
    for (u32 i = 0; i < t.fieldCount && field.empty(); ++i)
      if (t.fields[i].type == FieldType::Guid && !isObjectRef(t.name, t.fields[i].name) && prefabFields[t.fields[i].name] != noneFields[t.fields[i].name]) {
        field = t.fields[i].name;
        otherFields[field] = noneFields[field];
      }
  }
  if (field.empty()) { *why = "no overridable field (every field is a reference or an event)"; return false; }

  // The prefab: root with the required components and the type under test.
  PrefabAsset pa;
  pa.guid = kPrefabGuid;
  pa.rootFileId = pa.allocFileId();
  PrefabObject root;
  root.fileId = pa.rootFileId;
  root.name = "Subject";
  for (const std::string& rt : requiredTypes) {
    json rf = json::object();
    if (rt == "SkinnedMeshRenderer") rf = {{"mesh", assetGuid("SkinnedMeshRenderer", "mesh")}, {"material", assetGuid("SkinnedMeshRenderer", "material")}};
    root.components.push_back({pa.allocFileId(), rt, rf});
  }
  root.components.push_back({pa.allocFileId(), t.name, prefabFields});
  pa.objects.push_back(root);
  std::string err;
  spit(dir / "Assets/Prefabs/Subject.prefab", toJson(pa).dump(1));
  spit(dir / "Assets/Prefabs/Subject.prefab.meta", std::string("{\"guid\":\"") + kPrefabGuid + "\",\"importSettings\":{}}");
  json scene = {{"format", 1}, {"guid", kSceneGuid}, {"objects", json::array()}};
  spit(dir / "Assets/Scenes/matrix.scene", scene.dump(1));
  spit(dir / "Assets/Scenes/matrix.scene.meta", std::string("{\"guid\":\"") + kSceneGuid + "\",\"importSettings\":{}}");

  AssetDatabase db;
  db.open(dir);
  db.scan();
  PrefabLibrary lib;
  lib.open(&db);
  SceneDocument doc;
  if (!doc.load(dir / "Assets/Scenes/matrix.scene", err)) { *why = err; return false; }
  doc.setPrefabLibrary(&lib);
  const std::string over = doc.instantiatePrefab(kPrefabGuid, "");
  const std::string plain = doc.instantiatePrefab(kPrefabGuid, "");
  if (over.empty() || plain.empty()) { *why = "cannot instantiate the prefab"; return false; }
  const SceneObject* so = doc.find(over);
  int compIndex = -1;
  for (size_t i = 0; so && i < so->components.size(); ++i) if (so->components[i].type == t.name) compIndex = static_cast<int>(i);
  if (compIndex < 0) { *why = "the expanded instance has no " + std::string(t.name) + " component"; return false; }

  // The editor path: an inspector-style field edit on the instance becomes one override.
  if (!doc.setField(over, compIndex, field, otherFields[field])) { *why = "setField on the instance failed for " + field; return false; }
  if (!doc.isOverridden(over, compIndex, field) || doc.overrideCount(over) != 1) { *why = "the edit of " + field + " was not recorded as an override"; return false; }
  if (doc.overrideCount(plain) != 0) { *why = "an override leaked to the other instance"; return false; }
  if (!doc.save(err)) { *why = err; return false; }

  // Expected cooked values.
  auto expectedFor = [&](const std::string& rootGuid, bool overridden) {
    json j = prefabFields;
    for (auto it = j.begin(); it != j.end(); ++it)
      if (it.value().is_string() && it.value().get<std::string>() == "@1") it.value() = rootGuid;
    if (overridden) j[field] = otherFields[field];
    return j;
  };
  const json expOver = expectedFor(over, true), expPlain = expectedFor(plain, false);
  for (const char* target : {"linux", "psp"}) {
    std::string cookWhy;
    const std::vector<Found> got = cookedBlobs(dir, target, t, &cookWhy);
    if (!cookWhy.empty()) { *why = cookWhy; return false; }
    if (got.size() != 2) { *why = std::string(target) + ": expected 2 cooked " + t.name + " components, found " + std::to_string(got.size()); return false; }
    for (int k = 0; k < 2; ++k) {
      matrix::Obj cooked(t), expected(t);
      if (!readBlob(t, cooked.p(), got[static_cast<size_t>(k)].blob.data(), static_cast<u32>(got[static_cast<size_t>(k)].blob.size()))) { *why = std::string(target) + ": the cooked blob does not read back"; return false; }
      std::string ferr;
      if (!fieldsFromJson(t, expected.p(), k == 0 ? expOver : expPlain, &ferr)) { *why = "expected values do not load: " + ferr; return false; }
      const std::string d = matrix::firstDifference(t, expected.p(), cooked.p());
      if (!d.empty()) {
        *why = std::string(target) + ": instance " + (k == 0 ? "with" : "without") + " override: cooked field " + d + " differs" + (d == field ? " (the overridden field)" : " (a field the override must not touch)");
        return false;
      }
    }
  }

  // Revert after undo, then apply: the prefab file takes the value and the override is gone.
  if (!doc.revertField(over, compIndex, field) || doc.isOverridden(over, compIndex, field)) { *why = "revert did not drop the override"; return false; }
  if (!doc.undo() || !doc.isOverridden(over, compIndex, field)) { *why = "undo of the revert did not bring the override back"; return false; }
  if (!doc.applyField(over, compIndex, field) || doc.overrideCount(over) != 0) { *why = "apply left the override in the scene"; return false; }
  PrefabAsset after;
  if (!lib.get(kPrefabGuid, after, err)) { *why = err; return false; }
  const PrefabComponent* pc = nullptr;
  for (const PrefabComponent& c : after.objects[0].components) if (c.type == t.name) pc = &c;
  if (!pc || !pc->fields.contains(field) || pc->fields[field] != otherFields[field]) { *why = "apply did not write " + field + " into the prefab"; return false; }
  const SceneObject* plainAfter = doc.find(plain);
  matrix::Obj viaPlain(t), want(t);
  std::string ferr;
  json wantJson = expectedFor(plain, true);
  if (!plainAfter || !fieldsFromJson(t, want.p(), wantJson, &ferr)) { *why = "cannot check the other instance after apply"; return false; }
  for (const SceneComponent& c : plainAfter->components) if (c.type == t.name && !fieldsFromJson(t, viaPlain.p(), c.fields, &ferr)) { *why = "other instance fields: " + ferr; return false; }
  const std::string d = matrix::firstDifference(t, want.p(), viaPlain.p());
  if (!d.empty()) { *why = "after apply the other instance did not follow the prefab (field " + d + ")"; return false; }
  fs::remove_all(dir);
  return true;
}

struct Install {
  Install() { rensga::matrix::hooks().prefab = prefabStep; }
} g_install;

}  // namespace

TEST(component_matrix_prefab) {
  matrix::Options opt;
  opt.requirePrefab = true;
  std::vector<matrix::Row> rows;
  const int problems = matrix::run(opt, &rows);
  CHECK_EQ(problems, 0);
  size_t ok = 0;
  for (const matrix::Row& r : rows) if (r.prefab == matrix::State::Ok) ++ok;
  CHECK(ok >= 30);
}
