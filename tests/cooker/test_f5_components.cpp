// F5 contract: the cooker accepts the new components (Light, RenderSettings, SkinnedMeshRenderer, Animation, MeshRenderer.material,
// AudioSource.priority) and the new ConsoleTarget "features" / "limits", writes CTG2, and defaults the target from the project.
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;
using nlohmann::json;

static json readJson(const fs::path& p) {
  std::ifstream in(p);
  return json::parse(in);
}
static void writeJson(const fs::path& p, const json& j) { std::ofstream(p) << j.dump(1); }

TEST(cooker_f5_components_features_and_ctg2) {
  const fs::path src = fs::path(RENSGA_FIXTURES_DIR) / "cook_ok";
  const fs::path dst = fs::temp_directory_path() / "rensga_f5_cook";
  fs::remove_all(dst);
  fs::create_directories(dst);
  for (const char* d : {"Assets", "ProjectSettings"}) fs::copy(src / d, dst / d, fs::copy_options::recursive);

  json ct = readJson(dst / "ProjectSettings/ConsoleTarget.json");
  for (const char* c : {"Light", "RenderSettings", "SkinnedMeshRenderer", "Animation", "AudioSource"}) ct["allowedComponents"].push_back(c);
  ct["limits"] = {{"maxLightsPerObject", 3}, {"maxTextureSize", 256}};
  ct["features"] = json::array({"lighting", "fog"});
  writeJson(dst / "ProjectSettings/ConsoleTarget.json", ct);

  json scene = readJson(dst / "Assets/Scenes/test.scene");
  auto obj = [&](const char* guid, const char* name, json comps) {
    scene["objects"].push_back({{"guid", guid}, {"name", name}, {"parent", nullptr}, {"active", true},
                                {"transform", {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}},
                                {"components", comps}, {"prefab", nullptr}});
  };
  obj("00000000-0000-4000-8000-0000000000f1", "Lamp", json::array({{{"type", "Light"}, {"fields", {{"type", 1}, {"intensity", 2.0}, {"range", 6.0}, {"renderMode", 1}}}}}));
  obj("00000000-0000-4000-8000-0000000000f2", "Env", json::array({{{"type", "RenderSettings"}, {"fields", {{"fogEnabled", true}, {"fogEnd", 80.0}}}}}));
  obj("00000000-0000-4000-8000-0000000000f3", "Skinned",
      json::array({{{"type", "SkinnedMeshRenderer"}, {"fields", json::object()}},
                   {{"type", "Animation"}, {"fields", {{"playAutomatically", true}, {"wrapMode", 2}, {"speed", 1.5}}}}}));
  obj("00000000-0000-4000-8000-0000000000f4", "Speaker", json::array({{{"type", "AudioSource"}, {"fields", {{"priority", 7}}}}}));
  writeJson(dst / "Assets/Scenes/test.scene", scene);

  CookOptions o;
  o.project = dst;  // no target: the project's profile is used
  o.force = true;
  o.genHeader = dst / "Library/Generated/console_target.gen.h";
  CookResult r = cookProject(o, [](const CookMessage& m) { fprintf(stderr, "    [cook] %s\n", m.text.c_str()); });
  CHECK(r.ok);
  CHECK(fs::exists(dst / "Library/Cooked/psp/game.rpak"));

  std::ifstream in(r.packPath, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* p = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(p, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(p, bytes.size()));
  const PackConsoleTargetExt* e = view.consoleTargetExt();
  CHECK(e != nullptr);
  if (e) {
    CHECK_EQ(e->maxLightsPerObject, 3u);
    CHECK_EQ(e->maxBonesPerDraw, 8u);        // psp default from profile_defaults.json
    CHECK_EQ(e->maxTextureSize, 256u);       // project override
    CHECK_EQ(e->features, kTargetFeatLighting | kTargetFeatFog);
  }
  // CTGT keeps its 48-byte layout and values.
  const PackSection* ctgt = view.find(pack_section::CTGT);
  CHECK(ctgt != nullptr && ctgt->size == sizeof(PackConsoleTarget));

  Arena warena;
  warena.create(1 << 20);
  World world;
  WorldConfig cfg;
  cfg.maxObjects = 32;
  cfg.maxComponentsPerType = 32;
  CHECK(world.init(cfg, warena));
  LoadedPack lp;
  CHECK(world.loadPack(view, &lp));
  Light* li = world.findByName("Lamp")->GetComponent<Light>();
  CHECK(li && li->type == 1 && li->intensity == 2.0f && li->renderMode == 1);
  RenderSettings* rs = world.findByName("Env")->GetComponent<RenderSettings>();
  CHECK(rs && rs->fogEnabled && rs->fogEnd == 80.0f);
  GameObject* sk = world.findByName("Skinned");
  CHECK(sk && sk->GetComponent<SkinnedMeshRenderer>() != nullptr);
  Animation* an = sk->GetComponent<Animation>();
  CHECK(an && an->wrapMode == 2 && an->speed == 1.5f);
  AudioSource* as = world.findByName("Speaker")->GetComponent<AudioSource>();
  CHECK(as && as->priority == 7);

  // The generated header carries the features and the limits.
  std::ifstream hin(o.genHeader);
  std::string h((std::istreambuf_iterator<char>(hin)), std::istreambuf_iterator<char>());
  CHECK(h.find("#define ENGINE_FEATURE_LIGHTING 1") != std::string::npos);
  CHECK(h.find("#define ENGINE_FEATURE_FOG 1") != std::string::npos);
  CHECK(h.find("ENGINE_FEATURE_OSK") == std::string::npos);
  CHECK(h.find("kMaxLightsPerObject = 3u") != std::string::npos);

  // An unknown feature name is refused with the list of known ones.
  ct["features"] = json::array({"teleport"});
  writeJson(dst / "ProjectSettings/ConsoleTarget.json", ct);
  CookResult bad = cookProject(o, nullptr);
  CHECK(!bad.ok);
  bool named = false;
  for (const CookMessage& m : bad.errors) named = named || (m.text.find("unknown feature 'teleport'") != std::string::npos && m.text.find("cpuClock") != std::string::npos);
  CHECK(named);
  fs::remove_all(dst);
}

TEST(cooker_profile_defaults_are_data) {
  CHECK_EQ(profileRamLimitBytes("psp"), 32ull * 1024 * 1024);
  CHECK_EQ(profileRamLimitBytes("ps2"), 0ull);
  CHECK_EQ(profileRamLimitBytes("unknown-console"), 0ull);
  ConsoleTargetData t;
  t.features = {"osk", "cpuClock"};
  CHECK_EQ(t.featureBits(), kTargetFeatOsk | kTargetFeatCpuClock);
  CHECK(t.hasFeature("osk") && !t.hasFeature("fog"));
  CHECK_EQ(consoleTargetFeatureNames().size(), 8u);
}
