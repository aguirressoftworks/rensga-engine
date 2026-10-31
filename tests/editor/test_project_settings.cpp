// F6 E0: ProjectSettings schemas (TagManager, Physics, Time, Project build settings).
#include <filesystem>
#include <fstream>

#include "rensga/editor/project_settings.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
fs::path freshDir(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "ProjectSettings");
  return d;
}
void writeFile(const fs::path& p, const std::string& t) { std::ofstream(p, std::ios::binary) << t; }
}  // namespace

TEST(settings_missing_files_load_defaults) {
  fs::path d = freshDir("ps_missing");
  std::string err;
  TagManagerSettings t;
  CHECK(loadTagManager(d, t, err));
  CHECK_EQ(t.layers[0], std::string("Default"));
  CHECK(t.validate().empty());
  PhysicsSettings p;
  CHECK(loadPhysics(d, p, err));
  CHECK_NEAR(p.gravity[1], -9.81f, 1e-6f);
  CHECK(p.collides(3, 9));
  TimeSettings ti;
  CHECK(loadTime(d, ti, err));
  CHECK_NEAR(ti.fixedTimestep, 1.0 / 60.0, 1e-12);
  CHECK(ti.validate().empty());
}

TEST(settings_tagmanager_round_trip_and_validation) {
  fs::path d = freshDir("ps_tags");
  TagManagerSettings t = TagManagerSettings::defaults();
  t.tags = {"Enemy", "Pickup"};
  t.layers[6] = "Player";
  t.layers[31] = "Debug";
  t.extra["editorNote"] = "kept";
  std::string err;
  CHECK(saveTagManager(d, t, err));
  TagManagerSettings r;
  CHECK(loadTagManager(d, r, err));
  CHECK(r.tags == t.tags);
  CHECK_EQ(r.layers[31], std::string("Debug"));
  CHECK_EQ(r.layerIndex("Player"), 6);
  CHECK_EQ(r.layerIndex("Nope"), -1);
  CHECK(r.hasTag("Pickup"));
  CHECK_EQ(r.extra.value("editorNote", ""), std::string("kept"));
  CHECK(r.toJson() == t.toJson());

  TagManagerSettings bad = t;
  bad.tags.push_back("Enemy");
  bad.layers[7] = "Player";
  bad.layers[0] = "Base";
  CHECK(bad.validate().size() == 3);
  CHECK(!saveTagManager(d, bad, err));  // never writes an invalid file
  // a short layers array is padded
  writeFile(d / "ProjectSettings/TagManager.json", R"({"format":1,"layers":["Default","A"]})");
  CHECK(loadTagManager(d, r, err));
  CHECK_EQ(r.layers[1], std::string("A"));
  CHECK(r.layers[2].empty());
  writeFile(d / "ProjectSettings/TagManager.json", R"({"format":1,"tags":[1]})");
  CHECK(!loadTagManager(d, r, err));
  writeFile(d / "ProjectSettings/TagManager.json", R"({"format":9})");
  CHECK(!loadTagManager(d, r, err));
  writeFile(d / "ProjectSettings/TagManager.json", "{ not json");
  CHECK(!loadTagManager(d, r, err));
}

TEST(settings_physics_matrix) {
  fs::path d = freshDir("ps_phys");
  PhysicsSettings p;
  CHECK(p.collides(2, 5));
  p.setCollides(7, 6, false);  // stored normalised
  p.setCollides(2, 3, false);
  p.setCollides(2, 3, false);  // idempotent
  CHECK_EQ(p.ignore.size(), size_t(2));
  CHECK(!p.collides(6, 7) && !p.collides(7, 6) && p.collides(6, 6));
  const auto m = p.collisionMasks();
  CHECK_EQ(m[6], 0xFFFFFFFFu & ~(1u << 7));
  CHECK_EQ(m[7], 0xFFFFFFFFu & ~(1u << 6));
  CHECK_EQ(m[3], 0xFFFFFFFFu & ~(1u << 2));
  CHECK_EQ(m[0], 0xFFFFFFFFu);
  p.gravity[1] = -20.0f;
  p.queriesHitTriggers = false;
  std::string err;
  CHECK(savePhysics(d, p, err));
  PhysicsSettings r;
  CHECK(loadPhysics(d, r, err));
  CHECK(r.ignore == p.ignore);
  CHECK_NEAR(r.gravity[1], -20.0f, 1e-6f);
  CHECK(!r.queriesHitTriggers);
  p.setCollides(7, 6, true);
  CHECK_EQ(p.ignore.size(), size_t(1));
  writeFile(d / "ProjectSettings/Physics.json", R"({"format":1,"ignore":[[1,40]]})");
  CHECK(!loadPhysics(d, r, err));
  writeFile(d / "ProjectSettings/Physics.json", R"({"format":1,"gravity":[0,1]})");
  CHECK(!loadPhysics(d, r, err));
}

TEST(settings_time_validation) {
  fs::path d = freshDir("ps_time");
  TimeSettings t;
  t.fixedTimestep = 0.02;
  std::string err;
  CHECK(saveTime(d, t, err));
  TimeSettings r;
  CHECK(loadTime(d, r, err));
  CHECK_NEAR(r.fixedTimestep, 0.02, 1e-12);
  TimeSettings bad;
  bad.fixedTimestep = 0.0;
  CHECK(!bad.validate().empty());
  bad = TimeSettings();
  bad.maximumDelta = 0.001;  // below the fixed step
  CHECK(!bad.validate().empty());
  CHECK(!saveTime(d, bad, err));
}

TEST(settings_project_build_keeps_scenes_array_compatible) {
  fs::path d = freshDir("ps_project");
  // An older file: only "scenes". Every entry is enabled.
  writeFile(d / "ProjectSettings/Project.json", R"({"format":1,"name":"Old","target":"psp","scenes":["00000000-0000-4000-a000-000000000001"],"custom":7})");
  ProjectBuildSettings p;
  std::string err;
  CHECK(loadProjectBuild(d, p, err));
  CHECK_EQ(p.scenes.size(), size_t(1));
  CHECK(p.scenes[0].enabled);
  CHECK_EQ(p.target, std::string("psp"));
  CHECK(!p.development);
  CHECK(p.addScene("00000000-0000-4000-a000-000000000002", false));
  CHECK(!p.addScene("00000000-0000-4000-a000-000000000002"));
  p.development = true;
  CHECK(saveProjectBuild(d, p, err));
  // what the cooker reads: "scenes" lists the enabled scenes only
  std::ifstream f(d / "ProjectSettings/Project.json");
  json raw = json::parse(f);
  CHECK_EQ(raw["scenes"].size(), size_t(1));
  CHECK_EQ(raw["buildScenes"].size(), size_t(2));
  CHECK_EQ(raw["custom"].get<int>(), 7);
  CHECK_EQ(raw["format"].get<int>(), 1);
  ProjectBuildSettings r;
  CHECK(loadProjectBuild(d, r, err));
  CHECK_EQ(r.scenes.size(), size_t(2));
  CHECK(!r.scenes[1].enabled);
  CHECK(r.development);
  CHECK(r.setSceneEnabled("00000000-0000-4000-a000-000000000002", true));
  CHECK(r.moveScene(1, 0));
  CHECK_EQ(r.enabledScenes()[0], std::string("00000000-0000-4000-a000-000000000002"));
  CHECK(saveProjectBuild(d, r, err));
  std::ifstream f2(d / "ProjectSettings/Project.json");
  raw = json::parse(f2);
  CHECK_EQ(raw["scenes"].size(), size_t(2));
  CHECK_EQ(raw["scenes"][0].get<std::string>(), std::string("00000000-0000-4000-a000-000000000002"));
  CHECK(r.removeScene("00000000-0000-4000-a000-000000000001"));
  CHECK(!r.removeScene("nope"));
  r.scenes.push_back({"short", true});
  CHECK(!r.validate().empty());
}

TEST(settings_real_templates_load) {
  ProjectBuildSettings p;
  std::string err;
  CHECK(loadProjectBuild(RENSGA_TEMPLATE_DIR, p, err));
  CHECK(p.validate().empty());
  CHECK(!p.scenes.empty());
}
