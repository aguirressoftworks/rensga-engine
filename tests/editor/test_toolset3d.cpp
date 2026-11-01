// F6.02 (B3, Editor 8): the editor-core half of the 3D toolset: the material document with a target's limits, the Light / capsule /
// Rigidbody gizmos, and the Scene view's mesh list for a glTF level (samples/level_walk).
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "rensga/core/gizmos.h"
#include "rensga/editor/document_gizmos.h"
#include "rensga/editor/material_document.h"
#include "rensga/editor/scene_document.h"
#include "rensga/editor/viewport.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
fs::path tmp(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}
void put(const fs::path& p, const std::string& t) { std::ofstream(p, std::ios::binary) << t; }
}  // namespace

TEST(material_rules_come_from_the_target_features) {
  ConsoleTargetData open;  // no feature list: an older ConsoleTarget.json, everything allowed
  MaterialRules a = materialRulesFor(open);
  CHECK(a.lighting && a.fog && a.blend);
  ConsoleTargetData bare;
  bare.features = {"lighting"};
  bare.maxTextureSize = 512;
  MaterialRules b = materialRulesFor(bare);
  CHECK(b.lighting && !b.fog);
  CHECK(!b.noFogReason.empty());
  CHECK_EQ(b.maxTextureSize, 512u);
  ConsoleTargetData dark;
  dark.features = {"fog"};
  MaterialRules c = materialRulesFor(dark);
  CHECK(!c.lighting && c.fog);
  CHECK(!c.noLightingReason.empty());
}

TEST(material_document_edits_with_limits_undo_and_unknown_keys) {
  const fs::path d = tmp("mat");
  const fs::path f = d / "wall.mat.json";
  put(f, R"({"format":1,"texture":"00000000-0000-4000-8000-000000000604","color":[200,100,50,255],"unlit":false,"alphaTest":false,"note":"keep me"})");
  MaterialDocument m;
  std::string err;
  CHECK(m.load(f, err));
  CHECK(m.data().blend() == BlendMode::Opaque);
  MaterialRules all;
  CHECK(m.setColor(10, 20, 30));
  CHECK(m.setBlend(BlendMode::Alpha, all));
  CHECK(m.data().color[3] == 160 && m.data().blend() == BlendMode::Alpha);
  CHECK(m.setAlpha(80, all));
  CHECK(m.setUnlit(true, all));
  CHECK(m.setAlphaTest(true));
  CHECK(m.dirty());
  CHECK(m.save(err));
  CHECK(!m.dirty());
  const json saved = json::parse(std::ifstream(f));
  CHECK(saved["color"] == json::array({10, 20, 30, 80}));
  CHECK(saved["unlit"] == true && saved["alphaTest"] == true);
  CHECK(saved["note"] == "keep me");                       // keys this version does not know stay
  // undo walks back one edit at a time
  const size_t depth = m.undoDepth();
  CHECK(m.undo());
  CHECK(m.data().alphaTest == false);
  CHECK(m.redo());
  CHECK(m.data().alphaTest == true);
  CHECK(depth >= 5);
  // a target without lighting or blending refuses what it cannot draw
  MaterialRules nolight;
  nolight.lighting = false;
  nolight.blend = false;
  CHECK(!m.setUnlit(false, nolight));
  CHECK(m.data().unlit == true);
  CHECK(!m.setAlpha(100, nolight));
  CHECK(m.data().color[3] == 80);
  CHECK(!m.setBlend(BlendMode::Alpha, nolight) || m.data().blend() == BlendMode::Alpha);  // already alpha: nothing to refuse
  CHECK(m.setBlend(BlendMode::Opaque, nolight));
  CHECK(m.data().color[3] == 255);
  // normalize brings a material from another target inside the limits
  MaterialDocument m2;
  put(f, R"({"format":1,"color":[255,255,255,100],"unlit":false})");
  CHECK(m2.load(f, err));
  CHECK(m2.normalize(nolight));
  CHECK(m2.data().unlit && m2.data().color[3] == 255);
  CHECK(!m2.normalize(nolight));
  // errors
  put(f, "[1,2]");
  CHECK(!m2.load(f, err));
  put(f, R"({"color":[1,2,3]})");
  CHECK(!m2.load(f, err));
}

TEST(light_capsule_and_rigidbody_gizmos) {
  const fs::path d = tmp("giz");
  const fs::path scene = d / "S.scene";
  json s = {{"format", 1}, {"guid", "00000000-0000-4000-a000-000000000001"}, {"objects", json::array()}};
  auto obj = [&](const char* g, const char* name, json comps) {
    return json{{"guid", g}, {"name", name}, {"parent", nullptr}, {"active", true},
                {"transform", {{"position", {1, 2, 3}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}, {"components", comps}, {"prefab", nullptr}};
  };
  s["objects"].push_back(obj("00000000-0000-4000-9000-000000000001", "Lamp", json::array({{{"type", "Light"}, {"fields", {{"type", 1}, {"range", 6.0}, {"color", {255, 100, 50, 255}}}}}})));
  s["objects"].push_back(obj("00000000-0000-4000-9000-000000000002", "Sun", json::array({{{"type", "Light"}, {"fields", {{"type", 0}}}}})));
  s["objects"].push_back(obj("00000000-0000-4000-9000-000000000003", "Cap", json::array({{{"type", "CapsuleCollider"}, {"fields", {{"radius", 0.5}, {"height", 2.0}, {"direction", 1}}}}})));
  s["objects"].push_back(obj("00000000-0000-4000-9000-000000000004", "Body", json::array({{{"type", "Rigidbody"}, {"fields", {{"useGravity", true}}}}})));
  put(scene, s.dump());
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(scene, err));
  DocumentGizmoContext ctx;
  auto lines = [&](const char* guid) {
    doc.select(guid);
    GizmoDraw::clear();
    buildDocumentGizmos(doc, ctx);
    return GizmoDraw::lineCount();
  };
  const unsigned none = lines("");
  const unsigned lamp = lines("00000000-0000-4000-9000-000000000001");
  const unsigned sun = lines("00000000-0000-4000-9000-000000000002");
  const unsigned cap = lines("00000000-0000-4000-9000-000000000003");
  const unsigned body = lines("00000000-0000-4000-9000-000000000004");
  CHECK(none > 0);                  // the capsule collider and the faint light shapes are always drawn
  CHECK(lamp > none);               // selected point light: its range spheres appear
  CHECK(sun > none);                // selected directional light: the parallel rays
  CHECK(cap >= none);
  CHECK(body > none);               // selected Rigidbody: gravity arrow and axes
}

TEST(scene_view_lists_glTF_meshes_and_f5_lights) {
  const fs::path root = fs::path(RENSGA_LEVEL_DIR);
  if (!fs::exists(root / "Assets/Scenes/Level.scene")) { std::printf("skip: no samples/level_walk\n"); return; }
  MeshLibrary lib;
  lib.setProject(root);
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(root / "Assets/Scenes/Level.scene", err));
  SceneDraw sd = gatherScene(doc, lib);
  CHECK_EQ(sd.drawables.size(), 3u);               // Floor, Walls, Props: three glTF models
  for (const Drawable& dr : sd.drawables) CHECK(dr.mesh && dr.mesh->triangleCount() > 100);
  CHECK(sd.doc == &doc);
  bool lamp = false;
  for (const Marker& m : sd.markers) if (m.kind == "PointLight") lamp = true;
  CHECK(lamp);                                      // the F5 point lights are markers of their own kind
  CHECK(sd.light.dir[1] < 0);                       // the moon (a directional Light) lights the flat fallback
}
