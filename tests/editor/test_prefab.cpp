// F6.01 (Editor 6): prefabs. Expansion (nested, variants, precedence, cycles), instances in a SceneDocument (overrides recorded and
// undone like every edit, apply, revert, added and removed parts), prefab edit mode data, and the cooker: a scene with instances
// cooks to the same pack as the same scene expanded by hand.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

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
using nlohmann::ordered_json;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void spit(const fs::path& p, const std::string& t) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << t;
}

const char* kSceneGuid = "00000000-0000-4000-8000-00000000d0f1";

// A temporary project with an empty scene, a prefab folder, an asset database, a prefab library and a loaded document.
struct Proj {
  fs::path root;
  AssetDatabase db;
  PrefabLibrary lib;
  SceneDocument doc;
  explicit Proj(const char* name) {
    root = fs::path(RENSGA_TEST_TMP_DIR) / name;
    fs::remove_all(root);
    fs::create_directories(root / "Assets/Prefabs");
    fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok" / "ProjectSettings", root / "ProjectSettings", fs::copy_options::recursive);
    json ct = json::parse(slurp(root / "ProjectSettings/ConsoleTarget.json"));
    for (const char* n : {"Light", "Camera2D"}) ct["allowedComponents"].push_back(n);
    spit(root / "ProjectSettings/ConsoleTarget.json", ct.dump(1));
    json pj = json::parse(slurp(root / "ProjectSettings/Project.json"));
    pj["scenes"] = json::array({kSceneGuid});
    spit(root / "ProjectSettings/Project.json", pj.dump(1));
    json scene = {{"format", 1}, {"guid", kSceneGuid}, {"objects", json::array()}};
    spit(root / "Assets/Scenes/level.scene", scene.dump(1));
    spit(root / "Assets/Scenes/level.scene.meta", std::string("{\"guid\":\"") + kSceneGuid + "\",\"importSettings\":{}}");
    db.open(root);
    db.scan();
    lib.open(&db);
    std::string err;
    CHECK(doc.load(root / "Assets/Scenes/level.scene", err));
    doc.setPrefabLibrary(&lib);
  }
  fs::path prefabFolder() const { return root / "Assets/Prefabs"; }
  const SceneComponent* comp(const std::string& guid, const char* type) const {
    const SceneObject* o = doc.find(guid);
    if (!o) return nullptr;
    for (const SceneComponent& c : o->components) if (c.type == type) return &c;
    return nullptr;
  }
  double num(const std::string& guid, const char* type, const char* field) const {
    const SceneComponent* c = comp(guid, type);
    return c && c->fields.contains(field) ? c->fields[field].get<double>() : -999.0;
  }
  // Enemy (Light intensity 2) with a child Weapon (Light intensity 1): ids 1 (Enemy) and 2 (Weapon).
  std::string makeEnemyPrefab(std::string& enemyObject) {
    enemyObject = doc.createObject("Enemy", "", {{"Light", json{{"intensity", 2}, {"range", 10}}}});
    doc.createObject("Weapon", enemyObject, {{"Light", json{{"intensity", 1}, {"range", 4}}}});
    std::string err;
    std::string pg = doc.createPrefab(enemyObject, prefabFolder(), err);
    if (pg.empty()) fprintf(stderr, "createPrefab: %s\n", err.c_str());
    return pg;
  }
};

// An object with an explicit id (ids below nextFileId are used by hand in the expansion tests).
PrefabObject plainObject(PrefabAsset& a, const char* name, const FileId& parent, const FileId& id = FileId()) {
  PrefabObject o;
  o.fileId = id.empty() ? a.allocFileId() : id;
  o.name = name;
  o.parent = parent;
  return o;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------- expansion

TEST(prefab_expand_nested_variant_precedence) {
  std::map<std::string, PrefabAsset> files;
  // sword: a root (id 1) with a Light (intensity 1)
  PrefabAsset sword;
  sword.guid = "5a000000-0000-4000-8000-000000000001";
  sword.rootFileId = "1";
  sword.nextFileId = 10;
  { PrefabObject r = plainObject(sword, "Sword", "", "1"); r.components.push_back({"2", "Light", json{{"intensity", 1}, {"range", 5}}}); sword.objects.push_back(r); }
  files[sword.guid] = sword;
  // enemy: root 1 with a Light (7) and the nested sword at object 3 ("Hand"); the nested entry sets the sword's intensity to 2
  PrefabAsset enemy;
  enemy.guid = "5e000000-0000-4000-8000-000000000002";
  enemy.rootFileId = "1";
  enemy.nextFileId = 10;
  { PrefabObject r = plainObject(enemy, "Enemy", "", "1"); r.components.push_back({"2", "Light", json{{"intensity", 7}}}); enemy.objects.push_back(r); }
  { PrefabObject n = plainObject(enemy, "Hand", "1", "3"); n.nestedAsset = sword.guid; n.nestedRootFileId = "1";
    n.nested.overrides.push_back({"1", "Light.intensity", 0, json(2)}); enemy.objects.push_back(n); }
  files[enemy.guid] = enemy;
  // boss: a variant of enemy; it sets the nested sword's intensity to 3 and the enemy's to 9
  PrefabAsset boss;
  boss.guid = "5b000000-0000-4000-8000-000000000003";
  boss.variantBase = enemy.guid;
  boss.variant.overrides.push_back({"3/1", "Light.intensity", 0, json(3)});
  boss.variant.overrides.push_back({"1", "Light.intensity", 0, json(9)});
  files[boss.guid] = boss;
  PrefabLoader load = [&](const std::string& g, PrefabAsset& out, std::string& err) {
    auto it = files.find(g);
    if (it == files.end()) { err = "missing"; return false; }
    out = it->second;
    return true;
  };

  auto instanceOf = [&](const std::string& asset, const PrefabModifications& mods) {
    ordered_json root = ordered_json::object();
    root["guid"] = "10000000-0000-4000-8000-000000000001";
    root["name"] = "Inst";
    root["parent"] = nullptr;
    root["transform"] = {{"position", {1, 2, 3}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}};
    PrefabInstance inst;
    inst.asset = asset;
    inst.fileId = "1";
    inst.mods = mods;
    writeInstanceKeys(root, inst);
    return root;
  };
  auto intensityOf = [&](const std::vector<ordered_json>& objs, const std::string& name) {
    for (const ordered_json& o : objs) if (o.value("name", "") == name) return o["components"][0]["fields"].value("intensity", -1.0);
    return -2.0;
  };
  std::vector<ordered_json> out;
  std::string err;
  // enemy: base 7, nested sword 2 (nested layer beats the sword's own 1)
  CHECK(expandInstance(instanceOf(enemy.guid, {}), load, out, nullptr, err));
  CHECK_EQ(out.size(), 2u);
  CHECK_EQ(out[0].value("guid", ""), std::string("10000000-0000-4000-8000-000000000001"));
  CHECK_EQ(out[0]["transform"]["position"][2].get<double>(), 3.0);  // the root's placement is the scene's
  CHECK_EQ(intensityOf(out, "Inst"), 7.0);
  CHECK_EQ(intensityOf(out, "Hand"), 2.0);
  CHECK_EQ(out[1].value("parent", ""), out[0].value("guid", ""));
  // the member GUID is derived and stable
  CHECK_EQ(out[1].value("guid", ""), instanceObjectGuid(out[0].value("guid", ""), "3"));
  // variant: variant layer beats nested and base
  out.clear();
  CHECK(expandInstance(instanceOf(boss.guid, {}), load, out, nullptr, err));
  CHECK_EQ(intensityOf(out, "Inst"), 9.0);
  CHECK_EQ(intensityOf(out, "Hand"), 3.0);
  // instance layer beats the variant
  PrefabModifications m;
  m.overrides.push_back({"3/1", "Light.intensity", 0, json(4)});
  out.clear();
  CHECK(expandInstance(instanceOf(boss.guid, m), load, out, nullptr, err));
  CHECK_EQ(intensityOf(out, "Hand"), 4.0);
  CHECK_EQ(intensityOf(out, "Inst"), 9.0);  // unrelated field keeps the variant's value
  // removed object, removed component, added component
  PrefabModifications m2;
  m2.removedObjects.push_back("3");
  m2.addedComponents.push_back({"1", {"9", "Camera2D", json{{"deadZoneW", 2}}}});
  out.clear();
  CHECK(expandInstance(instanceOf(enemy.guid, m2), load, out, nullptr, err));
  CHECK_EQ(out.size(), 1u);
  CHECK_EQ(out[0]["components"].size(), 2u);
  CHECK_EQ(out[0]["components"][1].value("type", ""), std::string("Camera2D"));
}

TEST(prefab_expand_cycle_and_missing) {
  std::map<std::string, PrefabAsset> files;
  PrefabAsset a, b;
  a.guid = "6a000000-0000-4000-8000-000000000001";
  b.guid = "6b000000-0000-4000-8000-000000000002";
  for (PrefabAsset* p : {&a, &b}) {
    p->rootFileId = "1";
    p->nextFileId = 10;
    p->objects.push_back(plainObject(*p, p == &a ? "A" : "B", "", "1"));
  }
  { PrefabObject n = plainObject(a, "UsesB", a.rootFileId, "2"); n.nestedAsset = b.guid; n.nestedRootFileId = "1"; a.objects.push_back(n); }
  { PrefabObject n = plainObject(b, "UsesA", b.rootFileId, "2"); n.nestedAsset = a.guid; n.nestedRootFileId = "1"; b.objects.push_back(n); }
  files[a.guid] = a;
  files[b.guid] = b;
  PrefabLoader load = [&](const std::string& g, PrefabAsset& out, std::string& err) {
    auto it = files.find(g);
    if (it == files.end()) { err = "no such prefab"; return false; }
    out = it->second;
    return true;
  };
  PrefabNamer namer = [&](const std::string& g) { return g == a.guid ? std::string("A.prefab") : g == b.guid ? std::string("B.prefab") : g; };
  auto problems = checkPrefab(a.guid, load, namer);
  CHECK(!problems.empty());
  CHECK(problems[0].find("cycle") != std::string::npos);
  CHECK(problems[0].find("A.prefab") != std::string::npos && problems[0].find("B.prefab") != std::string::npos);
  // a missing base reports the asset
  PrefabAsset v;
  v.guid = "6c000000-0000-4000-8000-000000000003";
  v.variantBase = "6d000000-0000-4000-8000-0000000000ff";
  files[v.guid] = v;
  problems = checkPrefab(v.guid, load, namer);
  CHECK(!problems.empty() && problems[0].find("cannot be loaded") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------- scene document

TEST(prefab_create_instances_and_overrides_follow) {
  Proj p("prefab_follow");
  std::string enemy;
  const std::string pg = p.makeEnemyPrefab(enemy);
  CHECK(!pg.empty());
  CHECK(p.doc.isInstanceRoot(enemy));
  const std::string weaponGuid = instanceObjectGuid(enemy, "2");
  CHECK(p.doc.isPrefabMember(weaponGuid));
  CHECK(p.doc.find(weaponGuid) != nullptr);
  CHECK_EQ(p.doc.find(weaponGuid)->name, std::string("Weapon"));
  CHECK_EQ(p.doc.find(weaponGuid)->parentGuid, enemy);
  CHECK_EQ(p.num(enemy, "Light", "intensity"), 2.0);
  // the scene file holds the root only
  CHECK_EQ(p.doc.document()["objects"].size(), 1u);
  CHECK(p.doc.document()["objects"][0].contains("prefab"));
  // five instances
  std::vector<std::string> inst;
  for (int i = 0; i < 5; ++i) {
    float pos[3] = {float(i) * 2, 0, 0};
    inst.push_back(p.doc.instantiatePrefab(pg, "", pos));
    CHECK(!inst.back().empty());
  }
  CHECK_EQ(p.doc.scene().objects.size(), 2u * 6u);
  // override one field of the third instance
  const size_t depth = p.doc.undoDepth();
  CHECK(p.doc.setField(inst[2], 0, "intensity", json(5)));
  CHECK_EQ(p.doc.undoDepth(), depth + 1);
  CHECK_EQ(p.num(inst[2], "Light", "intensity"), 5.0);
  CHECK(p.doc.isOverridden(inst[2], 0, "intensity"));
  CHECK(!p.doc.isOverridden(inst[2], 0, "range"));
  CHECK(!p.doc.isOverridden(inst[1], 0, "intensity"));
  int overrides = 0;
  for (const ordered_json& o : p.doc.document()["objects"]) if (o.value("guid", "") == inst[2]) overrides = int(o["overrides"].size());
  CHECK_EQ(overrides, 1);
  // change the prefab: Enemy intensity 3, range 20, Weapon intensity 8
  PrefabAsset a;
  std::string err;
  CHECK(p.lib.get(pg, a, err));
  for (PrefabObject& o : a.objects) {
    if (o.name == "Enemy") { o.components[0].fields["intensity"] = 3; o.components[0].fields["range"] = 20; }
    if (o.name == "Weapon") o.components[0].fields["intensity"] = 8;
  }
  CHECK(p.lib.save(pg, a, err));
  CHECK(p.doc.refreshPrefabs());
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(p.num(inst[i], "Light", "range"), 20.0);
    CHECK_EQ(p.num(inst[i], "Light", "intensity"), i == 2 ? 5.0 : 3.0);  // the override survives, the other four follow
    CHECK_EQ(p.num(instanceObjectGuid(inst[i], "2"), "Light", "intensity"), 8.0);
  }
  // undo / redo of the override
  CHECK(p.doc.undo());
  CHECK_EQ(p.num(inst[2], "Light", "intensity"), 3.0);
  CHECK(p.doc.redo());
  CHECK_EQ(p.num(inst[2], "Light", "intensity"), 5.0);
  // setting the value back to the prefab's drops the override
  CHECK(p.doc.setField(inst[2], 0, "intensity", json(3)));
  CHECK(!p.doc.isOverridden(inst[2], 0, "intensity"));
  CHECK_EQ(p.doc.overrideCount(inst[2]), 0);
}

TEST(prefab_apply_revert_member_edits_undo) {
  Proj p("prefab_apply");
  std::string enemy;
  const std::string pg = p.makeEnemyPrefab(enemy);
  const std::string a = p.doc.instantiatePrefab(pg, ""), b = p.doc.instantiatePrefab(pg, "");
  const std::string weaponA = instanceObjectGuid(a, "2");
  // edits on members: field, name, active, transform
  CHECK(p.doc.setField(weaponA, 0, "intensity", json(6)));
  CHECK(p.doc.rename(weaponA, "Axe"));
  CHECK(p.doc.setActive(weaponA, false));
  float pos[3] = {4, 5, 6}, rot[4] = {0, 0, 0, 1}, sc[3] = {1, 1, 1};
  CHECK(p.doc.setTransform(weaponA, pos, rot, sc));
  CHECK_EQ(p.doc.find(weaponA)->name, std::string("Axe"));
  CHECK(!p.doc.find(weaponA)->active);
  CHECK_EQ(p.doc.find(weaponA)->position[2], 6.0f);
  CHECK(p.doc.isOverridden(weaponA, -1, "name"));
  CHECK(p.doc.hasObjectOverrides(weaponA));
  CHECK_EQ(p.doc.overrideCount(a), 4);  // intensity, name, active, position (rotation and scale did not change)
  // revert one field
  CHECK(p.doc.revertField(weaponA, 0, "intensity"));
  CHECK_EQ(p.num(weaponA, "Light", "intensity"), 1.0);
  CHECK(!p.doc.isOverridden(weaponA, 0, "intensity"));
  CHECK(p.doc.undo());
  CHECK_EQ(p.num(weaponA, "Light", "intensity"), 6.0);
  // apply the field: the prefab changes, the other instance follows, the override is gone
  CHECK(p.doc.applyField(weaponA, 0, "intensity"));
  CHECK(!p.doc.isOverridden(weaponA, 0, "intensity"));
  CHECK_EQ(p.num(weaponA, "Light", "intensity"), 6.0);
  CHECK_EQ(p.num(instanceObjectGuid(b, "2"), "Light", "intensity"), 6.0);
  PrefabAsset pa;
  std::string err;
  CHECK(p.lib.get(pg, pa, err));
  CHECK_EQ(pa.find("2")->components[0].fields["intensity"].get<double>(), 6.0);
  // undo restores both the prefab file and the override
  CHECK(p.doc.undo());
  CHECK_EQ(p.num(instanceObjectGuid(b, "2"), "Light", "intensity"), 1.0);
  CHECK_EQ(p.num(weaponA, "Light", "intensity"), 6.0);
  CHECK(p.doc.isOverridden(weaponA, 0, "intensity"));
  CHECK(p.doc.redo());
  CHECK_EQ(p.num(instanceObjectGuid(b, "2"), "Light", "intensity"), 6.0);
  // apply the whole object (name, active, position) and then everything reverts cleanly
  CHECK(p.doc.applyObject(weaponA));
  CHECK_EQ(p.doc.find(instanceObjectGuid(b, "2"))->name, std::string("Axe"));
  CHECK(!p.doc.find(instanceObjectGuid(b, "2"))->active);
  CHECK(!p.doc.hasObjectOverrides(weaponA));
  CHECK_EQ(p.doc.overrideCount(a), 0);
  // revert the whole instance after new edits
  CHECK(p.doc.setField(a, 0, "range", json(99)));
  CHECK(p.doc.setTag(a, "Boss"));
  CHECK_EQ(p.doc.overrideCount(a), 2);
  CHECK(p.doc.revertInstance(a));
  CHECK_EQ(p.doc.overrideCount(a), 0);
  CHECK_EQ(p.num(a, "Light", "range"), 10.0);
  CHECK(p.doc.find(a)->tag.empty());
}

TEST(prefab_added_removed_parts_and_unpack) {
  Proj p("prefab_parts");
  std::string enemy;
  const std::string pg = p.makeEnemyPrefab(enemy);
  const std::string a = p.doc.instantiatePrefab(pg, ""), b = p.doc.instantiatePrefab(pg, "");
  const std::string weaponA = instanceObjectGuid(a, "2");
  // added component on a member, removed component on the root
  CHECK(p.doc.addComponent(weaponA, "Camera2D", json{{"deadZoneW", 3}}));
  CHECK_EQ(p.doc.find(weaponA)->components.size(), 2u);
  CHECK(p.doc.setField(weaponA, 1, "deadZoneW", json(4)));  // edits the added component's own fields, no override
  CHECK_EQ(p.doc.overrideCount(a), 1);
  CHECK_EQ(p.num(weaponA, "Camera2D", "deadZoneW"), 4.0);
  CHECK(p.doc.removeComponent(a, 0));  // the root's Light
  CHECK(p.doc.find(a)->components.empty());
  CHECK_EQ(p.doc.find(b)->components.size(), 1u);
  CHECK(p.doc.undo());
  CHECK_EQ(p.doc.find(a)->components.size(), 1u);
  // a new child of a member is stored in the instance
  const std::string extra = p.doc.createObject("Gem", weaponA, {});
  CHECK(!extra.empty());
  CHECK(p.doc.isAddedToInstance(extra));
  CHECK_EQ(p.doc.find(extra)->parentGuid, weaponA);
  CHECK_EQ(p.doc.document()["objects"].size(), 3u);  // enemy, a, b: the Gem lives inside a's addedObjects
  CHECK(p.doc.setField(extra, 0, "x", json(1)) == false);  // no components
  CHECK(p.doc.rename(extra, "Gem2"));
  CHECK_EQ(p.doc.find(extra)->name, std::string("Gem2"));
  // removing a member of the prefab: recorded in removedObjects, the other instance keeps it
  CHECK(p.doc.remove(weaponA));
  CHECK(p.doc.find(weaponA) == nullptr);
  CHECK(p.doc.find(extra) == nullptr);  // its added child went with it
  CHECK(p.doc.find(instanceObjectGuid(b, "2")) != nullptr);
  CHECK(p.doc.undo());
  CHECK(p.doc.find(weaponA) != nullptr);
  CHECK(p.doc.find(extra) != nullptr);
  // members cannot be reparented or duplicated alone
  CHECK(!p.doc.reparentMany({weaponA}, "", ""));
  CHECK(p.doc.duplicateMany({weaponA}).empty());
  // duplicating an instance root clones its modifications with fresh GUIDs
  const std::string dup = p.doc.duplicate(a);
  CHECK(!dup.empty() && dup != a);
  CHECK(p.doc.isInstanceRoot(dup));
  CHECK(p.doc.find(instanceObjectGuid(dup, "2")) != nullptr);
  size_t gems = 0;
  for (const SceneObject& o : p.doc.scene().objects) if (o.name == "Gem2") ++gems;
  CHECK_EQ(gems, 2u);
  std::set<std::string> seen;
  for (const SceneObject& o : p.doc.scene().objects) CHECK(seen.insert(o.guid).second);  // every GUID unique
  // unpack: plain objects, the same GUIDs, no link
  CHECK(p.doc.unpackPrefab(b));
  CHECK(!p.doc.isInstanceRoot(b));
  CHECK(p.doc.find(instanceObjectGuid(b, "2")) != nullptr);
  CHECK(!p.doc.document()["objects"].is_null());
  for (const ordered_json& o : p.doc.document()["objects"]) if (o.value("guid", "") == b) CHECK(!o.contains("overrides") && (!o.contains("prefab") || o["prefab"].is_null()));
  CHECK(p.doc.undo());
  CHECK(p.doc.isInstanceRoot(b));
}

TEST(prefab_variant_and_nested_from_scene) {
  Proj p("prefab_variant");
  std::string enemy;
  const std::string pg = p.makeEnemyPrefab(enemy);
  const std::string a = p.doc.instantiatePrefab(pg, "");
  CHECK(p.doc.setField(a, 0, "intensity", json(9)));
  // an instance with overrides becomes a variant
  std::string err;
  const std::string vg = p.doc.createPrefab(a, p.prefabFolder(), err);
  CHECK(!vg.empty());
  PrefabAsset v;
  CHECK(p.lib.get(vg, v, err));
  CHECK(v.isVariant());
  CHECK_EQ(v.variantBase, pg);
  CHECK_EQ(p.doc.overrideCount(a), 0);
  CHECK_EQ(p.num(a, "Light", "intensity"), 9.0);       // the variant carries the override
  CHECK_EQ(p.doc.prefabOf(a), vg);
}

// ---------------------------------------------------------------------------------------------------------- stage

TEST(prefab_stage_round_trip) {
  Proj p("prefab_stage");
  std::string enemy;
  const std::string pg = p.makeEnemyPrefab(enemy);
  PrefabAsset a;
  std::string err;
  CHECK(p.lib.get(pg, a, err));
  ordered_json stage = prefabToStage(a);
  CHECK_EQ(stage["objects"].size(), 2u);
  PrefabAsset b;
  CHECK(stageToPrefab(stage, b, err));
  CHECK(toJson(a) == toJson(b));
  // a new object in the stage gets a new id, ids of the others stay
  ordered_json extra = stage["objects"][1];
  extra["guid"] = "20000000-0000-4000-8000-000000000009";
  extra.erase("fileId");
  extra["name"] = "Shield";
  stage["objects"].push_back(extra);
  PrefabAsset c;
  CHECK(stageToPrefab(stage, c, err));
  CHECK_EQ(c.objects.size(), 3u);
  CHECK_EQ(c.objects[0].fileId, a.objects[0].fileId);
  CHECK_EQ(c.objects[2].fileId, std::to_string(a.nextFileId));
  CHECK(c.nextFileId > a.nextFileId);
  // two roots are refused
  stage["objects"][2]["parent"] = nullptr;
  CHECK(!stageToPrefab(stage, c, err));
}

// ---------------------------------------------------------------------------------------------------------- cooker

namespace {
std::vector<uint8_t> cookPack(const fs::path& project, std::string* firstError) {
  CookOptions o;
  o.project = project;
  o.target = "psp";
  o.force = true;
  CookResult r = cookProject(o, [&](const CookMessage& m) {
    if (m.severity == CookMessage::Error && firstError && firstError->empty()) *firstError = m.text;
  });
  if (!r.ok) return {};
  std::ifstream in(r.packPath, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

TEST(prefab_cook_matches_manual_expansion) {
  Proj p("prefab_cook");
  std::string enemy;
  const std::string pg = p.makeEnemyPrefab(enemy);
  std::vector<std::string> inst;
  for (int i = 0; i < 3; ++i) {
    float pos[3] = {float(i) * 3, 0, 0};
    inst.push_back(p.doc.instantiatePrefab(pg, "", pos));
  }
  CHECK(p.doc.setField(inst[1], 0, "intensity", json(5)));
  CHECK(p.doc.setField(instanceObjectGuid(inst[2], "2"), 0, "range", json(2.5)));
  CHECK(p.doc.addComponent(inst[0], "Camera2D", json{{"deadZoneW", 2}}));
  const std::string gem = p.doc.createObject("Gem", instanceObjectGuid(inst[0], "2"), {{"Light", json{{"intensity", 0.5}}}});
  CHECK(!gem.empty());
  std::string err;
  CHECK(p.doc.save(err));

  // the same scene written out by hand: the editor's expanded view, saved as ordinary objects
  fs::path manual = fs::path(RENSGA_TEST_TMP_DIR) / "prefab_cook_manual";
  fs::remove_all(manual);
  fs::create_directories(manual);
  fs::create_directories(manual / "Assets");
  fs::copy(p.root / "ProjectSettings", manual / "ProjectSettings", fs::copy_options::recursive);
  fs::copy(p.root / "Assets/Scenes", manual / "Assets/Scenes", fs::copy_options::recursive);
  json scene = json::parse(slurp(p.root / "Assets/Scenes/level.scene"));
  ordered_json expanded;
  std::vector<std::string> errs;
  CHECK(expandSceneObjects(ordered_json::parse(scene["objects"].dump()), p.lib.loader(), expanded, nullptr, errs));
  CHECK(errs.empty());
  CHECK_EQ(expanded.size(), p.doc.scene().objects.size());
  scene["objects"] = json::parse(expanded.dump());
  spit(manual / "Assets/Scenes/level.scene", scene.dump(1));

  std::string e1, e2;
  const std::vector<uint8_t> packA = cookPack(p.root, &e1), packB = cookPack(manual, &e2);
  if (packA.empty()) fprintf(stderr, "cook (prefab): %s\n", e1.c_str());
  if (packB.empty()) fprintf(stderr, "cook (manual): %s\n", e2.c_str());
  CHECK(!packA.empty());
  CHECK(packA == packB);  // byte-identical
}

TEST(prefab_cook_template_for_guid_field) {
  Proj p("prefab_template");
  std::string enemy;
  const std::string pg = p.makeEnemyPrefab(enemy);
  // a spawner whose Camera2D.target names the prefab asset: the cook adds an inactive template and points the field at it
  const std::string spawner = p.doc.createObject("Spawner", "", {{"Camera2D", json{{"target", pg}}}});
  CHECK(!spawner.empty());
  std::string err;
  CHECK(p.doc.unpackPrefab(enemy));  // the scene's own Enemy becomes plain objects
  CHECK(p.doc.save(err));
  std::string e1;
  const std::vector<uint8_t> pack = cookPack(p.root, &e1);
  if (pack.empty()) fprintf(stderr, "cook (template): %s\n", e1.c_str());
  CHECK(!pack.empty());
  u8* mem = static_cast<u8*>(aligned_alloc(16, (pack.size() + 15) & ~size_t(15)));
  memcpy(mem, pack.data(), pack.size());
  PackView view;
  CHECK(view.open(mem, pack.size(), "template"));
  u32 n = 0;
  view.records<PackObject>(pack_section::OBJS, &n);
  CHECK_EQ(n, 5u);  // Spawner, the unpacked Enemy and Weapon, plus the template's two objects
  free(mem);
}
