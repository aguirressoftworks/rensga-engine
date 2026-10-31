// F6 E0: prefab file format (data + JSON only).
#include <filesystem>
#include <set>

#include "rensga/editor/asset_stubs.h"
#include "rensga/editor/prefab_format.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

namespace {
PrefabAsset makeEnemy() {
  PrefabAsset p;
  p.guid = "11111111-1111-4111-8111-111111111111";
  p.rootFileId = p.allocFileId();  // 1
  PrefabObject root;
  root.fileId = p.rootFileId;
  root.name = "Enemy";
  root.components.push_back({p.allocFileId(), "MeshRenderer", json{{"mesh", "22222222-2222-4222-8222-222222222222"}}});
  root.components.push_back({p.allocFileId(), "Enemy", json{{"speed", 2}}});
  p.objects.push_back(root);
  PrefabObject weapon;
  weapon.fileId = p.allocFileId();
  weapon.name = "Weapon";
  weapon.parent = p.rootFileId;
  weapon.position[0] = 1.5f;
  weapon.nestedAsset = "33333333-3333-4333-8333-333333333333";
  weapon.nestedRootFileId = "1";
  weapon.nested.overrides.push_back({"1", "MeshRenderer.color", 0, json::array({255, 0, 0, 255})});
  p.objects.push_back(weapon);
  return p;
}
}  // namespace

TEST(prefab_file_id_forms) {
  CHECK_EQ(fileIdFromJson(json(7)), std::string("7"));
  CHECK_EQ(fileIdFromJson(json("3/7")), std::string("3/7"));
  CHECK(fileIdFromJson(json("3//7")).empty());
  CHECK(fileIdFromJson(json("x")).empty());
  CHECK(fileIdFromJson(json(nullptr)).empty());
  CHECK(fileIdToJson("12").is_number());
  CHECK(fileIdToJson("3/7").is_string());
}

TEST(prefab_round_trip_and_validate) {
  PrefabAsset p = makeEnemy();
  CHECK(validate(p).empty());
  const ordered_json j = toJson(p);
  PrefabAsset r;
  std::string err;
  CHECK(fromJson(json::parse(j.dump()), r, err));
  CHECK(toJson(r) == j);
  CHECK_EQ(r.objects.size(), size_t(3 - 1));
  CHECK_EQ(r.objects[1].nested.overrides[0].path, std::string("MeshRenderer.color"));
  CHECK_NEAR(r.objects[1].position[0], 1.5f, 1e-6f);
  CHECK_EQ(r.nextFileId, 5LL);
  const fs::path f = fs::path(RENSGA_TEST_TMP_DIR) / "enemy.prefab";
  fs::create_directories(f.parent_path());
  CHECK(savePrefab(f.string(), p, err));
  PrefabAsset l;
  CHECK(loadPrefab(f.string(), l, err));
  CHECK(toJson(l) == j);
}

TEST(prefab_validation_finds_structure_errors) {
  PrefabAsset p = makeEnemy();
  p.objects[1].parent = "99";
  CHECK(!validate(p).empty());
  p = makeEnemy();
  p.objects.push_back(p.objects[1]);  // duplicate id
  CHECK(!validate(p).empty());
  p = makeEnemy();
  p.objects[0].parent = "2";  // no root any more / cycle
  CHECK(!validate(p).empty());
  p = makeEnemy();
  p.objects[1].nested.overrides.push_back({"1", "nodot", 0, json(1)});
  CHECK(!validate(p).empty());
  p = makeEnemy();
  p.objects[1].nestedAsset = p.guid;  // contains itself
  CHECK(!validate(p).empty());
  p = makeEnemy();
  p.nextFileId = 2;  // ids must stay below nextFileId
  CHECK(!validate(p).empty());
}

TEST(prefab_variant_round_trip) {
  PrefabAsset v;
  v.guid = "44444444-4444-4444-8444-444444444444";
  v.variantBase = "11111111-1111-4111-8111-111111111111";
  v.variant.overrides.push_back({"3", "Enemy.speed", 0, json(4)});
  v.variant.removedComponents.push_back({"2", "MeshRenderer", 0});
  v.variant.addedComponents.push_back({"1", {"1", "AudioSource", json{{"volume", 0.5}}}});
  v.variant.removedObjects.push_back("4");
  CHECK(validate(v).empty());
  PrefabAsset r;
  std::string err;
  CHECK(fromJson(json::parse(toJson(v).dump()), r, err));
  CHECK(r.isVariant());
  CHECK_EQ(r.variantBase, v.variantBase);
  CHECK_EQ(r.variant.overrides.size(), size_t(1));
  CHECK_EQ(r.variant.addedComponents[0].component.type, std::string("AudioSource"));
  CHECK_EQ(r.variant.removedObjects[0], std::string("4"));
  CHECK(toJson(r) == toJson(v));
  json bad = json::parse(toJson(v).dump());
  bad["objects"] = json::array();
  CHECK(!fromJson(bad, r, err));
  v.variantBase = v.guid;
  CHECK(!validate(v).empty());
}

TEST(prefab_scene_instance_keys) {
  PrefabInstance in;
  in.asset = "11111111-1111-4111-8111-111111111111";
  in.fileId = "1";
  in.mods.overrides.push_back({"3", "Enemy.speed", 0, json(5)});
  in.mods.overrides.push_back({"4/1", "@name", 0, json("Axe")});
  in.mods.overrides.push_back({"2", "MeshRenderer.color.1", 1, json(10)});
  in.mods.removedObjects.push_back("4");
  ordered_json so = ordered_json::object();
  so["guid"] = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
  so["name"] = "Enemy (1)";
  writeInstanceKeys(so, in);
  CHECK(so["prefab"]["asset"] == in.asset);
  CHECK_EQ(so["overrides"][1]["fileId"].get<std::string>(), std::string("4/1"));
  CHECK(!so.contains("addedComponents"));  // empty lists are omitted
  PrefabInstance out;
  std::string err;
  CHECK(readInstanceKeys(json::parse(so.dump()), out, err));
  CHECK_EQ(out.mods.overrides.size(), size_t(3));
  CHECK_EQ(out.mods.overrides[2].occurrence, 1);
  CHECK_EQ(out.mods.removedObjects[0], std::string("4"));
  json plain = {{"guid", "x"}, {"prefab", nullptr}};  // old scenes
  CHECK(!readInstanceKeys(plain, out, err));
  CHECK(err.empty());
  json broken = {{"prefab", {{"asset", "g"}, {"fileId", 1}}}, {"overrides", json::array({json{{"fileId", 1}}})}};
  CHECK(!readInstanceKeys(broken, out, err));
}

TEST(prefab_instance_guids_are_deterministic_and_distinct) {
  const std::string root = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
  const std::string a = instanceObjectGuid(root, "3"), b = instanceObjectGuid(root, "3"), c = instanceObjectGuid(root, "4"),
                    d = instanceObjectGuid("bbbbbbbb-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "3");
  CHECK_EQ(a, b);
  CHECK(a != c && a != d && c != d);
  CHECK_EQ(a.size(), size_t(36));
  CHECK_EQ(a[14], '5');
  std::set<std::string> seen;
  for (int i = 0; i < 500; ++i) seen.insert(instanceObjectGuid(root, std::to_string(i)));
  CHECK_EQ(seen.size(), size_t(500));
}

TEST(asset_kinds_f6) {
  CHECK(assetKindOf("Assets/Enemy.prefab") == AssetKind::Prefab);
  CHECK(assetKindOf("Assets/Red.mat.json") == AssetKind::Material);
  CHECK(assetKindOf("Assets/Hero.controller.json") == AssetKind::AnimatorController);
  CHECK(assetKindOf("Assets/Door.clip.json") == AssetKind::PropertyClip);
  CHECK(assetKindOf("Assets/Stats.asset.json") == AssetKind::DataAsset);
  CHECK(assetKindOf("Assets/Hero.anim.json") == AssetKind::Animation);  // 2D sprite animation keeps its kind
  CHECK(assetKindOf("Assets/other.json") == AssetKind::Data);
  CHECK(std::string(assetKindName(AssetKind::Prefab)) == "prefab");
  CHECK(std::string(assetKindName(AssetKind::Material)) == "material");
}

TEST(asset_stubs_create_import_and_pick_free_names) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / "stubs";
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "Assets/Prefabs");
  fs::create_directories(d / "ProjectSettings");
  AssetDatabase db;
  db.open(d);
  db.scan();
  int n = 0;
  stubKinds(&n);
  CHECK_EQ(n, 5);
  for (int i = 0; i < n; ++i) {
    const StubKind& k = stubKinds(nullptr)[i];
    fs::path made, again;
    std::string err;
    CHECK(createAssetStub(db, k.kind, d / "Assets/Prefabs", "", made, err));
    CHECK(createAssetStub(db, k.kind, d / "Assets/Prefabs", "", again, err));
    CHECK(made != again);  // "New X" then "New X 1"
    const AssetEntry* e = db.findPath(made);
    CHECK(e != nullptr);
    CHECK(e && e->kind == k.kind);
    CHECK(e && fs::exists(made.string() + ".meta"));
    CHECK(e && e->meta.value("importer", "") == assetKindName(k.kind));
  }
  fs::path pf;
  std::string err;
  CHECK(createAssetStub(db, AssetKind::Prefab, d / "Assets", "Boss", pf, err));
  PrefabAsset pa;
  CHECK(loadPrefab(pf.string(), pa, err));
  CHECK_EQ(pa.guid, db.findPath(pf)->guid);  // the prefab's own guid mirrors its .meta
  CHECK(validate(pa).empty());
  CHECK_EQ(pa.objects[0].name, std::string("Boss"));
  CHECK(!createAssetStub(db, AssetKind::Texture, d / "Assets", "x", pf, err));
  CHECK(!createAssetStub(db, AssetKind::Prefab, d / "Assets", "a/b", pf, err));
}
