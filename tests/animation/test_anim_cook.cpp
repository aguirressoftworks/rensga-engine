// F6.03 (B1): .clip.json / .controller.json -> PCLP / ANIC, then the cooked pack plays in the Animator runtime (cook -> load -> run),
// identical bytes for every target, and clear errors that name the asset.
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "rensga/core/animation.h"
#include "rensga/core/animator_system.h"
#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/prop_clip.h"
#include "rensga/core/world.h"
#include "rensga/cooker/anim_cook.h"
#include "rensga/cooker/cooker.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::cooker;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
const char* kScene = "00000000-0000-4000-8000-00000000d0f1";
const char* kObj = "00000000-0000-4000-8000-0000000000a1";
const char* kClipIdle = "00000000-0000-4000-8000-00000000c101";
const char* kClipRun = "00000000-0000-4000-8000-00000000c102";
const char* kCtl = "00000000-0000-4000-8000-00000000c201";
const char* kTex = "00000000-0000-4000-8000-00000000c301";

bool writeText(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream o(p, std::ios::binary);
  o << s;
  return bool(o);
}
void writeAsset(const fs::path& dir, const char* rel, const char* guid, const json& doc) {
  writeText(dir / rel, doc.dump(1));
  writeText(dir / (std::string(rel) + ".meta"), std::string("{\"guid\":\"") + guid + "\",\"importSettings\":{}}");
}

json idleClip() {
  return json::parse(R"({"format":1,"kind":"PropertyClip","name":"Idle","fps":60,"length":60,"loop":true,
    "curves":[{"property":"SpriteRenderer.srcX","interp":"step","keys":[{"t":0,"v":0},{"t":30,"v":16}]}],
    "events":[{"t":30,"function":"Blink","int":2}]})");
}
json runClip() {
  return json::parse(R"({"format":1,"kind":"PropertyClip","name":"Run","fps":30,"length":30,"loop":true,
    "curves":[{"property":"SpriteRenderer.srcX","interp":"step","keys":[{"t":0,"v":100},{"t":15,"v":116}]},
              {"property":"Transform.localPosition.x","interp":"linear","keys":[{"t":0,"v":0},{"t":30,"v":3}]},
              {"property":"SpriteRenderer.color.r","keys":[{"t":0,"v":255},{"t":30,"v":0}]},
              {"property":"SpriteRenderer.texture","keys":[{"t":0,"v":"00000000-0000-4000-8000-00000000c301"},{"t":15,"v":"00000000-0000-4000-8000-00000000c301"}]}]})");
}
json controller() {
  return json::parse(std::string(R"({"format":1,"kind":"AnimatorController","name":"Player",
    "parameters":[{"name":"speed","type":"float","default":0},{"name":"jump","type":"trigger"}],
    "defaultState":"Idle",
    "states":[{"name":"Idle","motion":")") + kClipIdle + R"(","loop":true},{"name":"Run","motion":")" + kClipRun + R"(","loop":true}],
    "transitions":[{"from":"Idle","to":"Run","duration":0.1,"conditions":[{"param":"speed","mode":"greater","value":0.5}]},
                   {"from":"Run","to":"Idle","conditions":[{"param":"speed","mode":"less","value":0.5}]}]})");
}

fs::path makeProject(const char* name, const json& ctl, bool badControllerRef = false, bool withAnim = true) {
  const fs::path dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok" / "ProjectSettings", dir / "ProjectSettings", fs::copy_options::recursive);
  json ct;
  {
    std::ifstream in(dir / "ProjectSettings/ConsoleTarget.json");
    ct = json::parse(in);
  }
  ct["allowedComponents"].push_back("SpriteRenderer");
  ct["allowedComponents"].push_back("Animator");
  writeText(dir / "ProjectSettings/ConsoleTarget.json", ct.dump(1));
  if (withAnim) {
    writeAsset(dir, "Assets/Anim/Idle.clip.json", kClipIdle, idleClip());
    writeAsset(dir, "Assets/Anim/Run.clip.json", kClipRun, runClip());
    writeAsset(dir, "Assets/Anim/Player.controller.json", kCtl, ctl);
  }
  json scene = {{"format", 1}, {"guid", kScene}, {"objects", json::array()}};
  json comps = json::array();
  comps.push_back({{"type", "SpriteRenderer"}, {"fields", json::object()}});
  if (withAnim) comps.push_back({{"type", "Animator"}, {"fields", {{"controller", badControllerRef ? "00000000-0000-4000-8000-00000000ffff" : kCtl}}}});
  scene["objects"].push_back({{"guid", kObj}, {"name", "Hero"}, {"parent", nullptr}, {"active", true},
                              {"transform", {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}},
                              {"components", comps}, {"prefab", nullptr}});
  writeText(dir / "Assets/Scenes/anim.scene", scene.dump(1));
  writeText(dir / "Assets/Scenes/anim.scene.meta", std::string("{\"guid\":\"") + kScene + "\",\"importSettings\":{}}");
  (void)kTex;
  return dir;
}

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
struct Loaded {
  u8* mem = nullptr;
  PackView view;
  ~Loaded() { free(mem); }
  bool open(const std::vector<uint8_t>& bytes) {
    mem = static_cast<u8*>(aligned_alloc(16, (bytes.size() + 15) & ~size_t(15)));
    memcpy(mem, bytes.data(), bytes.size());
    return view.open(mem, bytes.size(), "anim");
  }
};
}  // namespace

TEST(anim_cook_bakes_clip_records) {
  std::vector<u8> blob;
  std::vector<std::string> strs{""};
  AddStringFn add = [&](const std::string& s) { if (s.empty()) return 0u; strs.push_back(s); return static_cast<u32>(strs.size() - 1); };
  PackPropClip c;
  std::string err;
  const json doc = json::parse(R"({"format":1,"name":"X","fps":24,"loop":true,
     "curves":[{"target":"Arm/Hand","property":"SpriteRenderer.color.g","interp":"smooth","keys":[{"t":10,"v":5,"in":1,"out":2},{"t":0,"v":1}]},
               {"property":"GameObject.active","keys":[{"t":0,"v":true},{"t":4,"v":false}]}],
     "events":[{"t":8,"function":"B"},{"t":2,"function":"A","float":1.5,"string":"s"}]})");
  CHECK(bakePropClip(doc, Guid(), add, blob, c, err));
  CHECK_EQ(c.fps, 24u);
  CHECK_EQ(c.lengthTicks, 10u);  // no "length": the last key
  CHECK_EQ(c.flags, kPropClipLoop);
  CHECK_EQ(c.curveCount, 2u);
  CHECK_EQ(c.eventCount, 2u);
  CHECK_EQ(c.curvesOffset % 16, 0u);
  const PackPropCurve* pc = reinterpret_cast<const PackPropCurve*>(blob.data() + c.curvesOffset);
  CHECK_EQ(pc[0].interp, kPropInterpHermite);
  CHECK_EQ(pc[0].part, 1);
  CHECK_EQ(pc[0].valueType, static_cast<u8>(FieldType::Color32));
  CHECK_EQ(pc[0].componentType, fnv1a32("SpriteRenderer"));
  CHECK_EQ(pc[0].fieldHash, fnv1a32("color"));
  CHECK(strs[pc[0].pathStr] == "Arm/Hand");
  const PackPropKey* k = reinterpret_cast<const PackPropKey*>(blob.data() + pc[0].keysOffset);
  CHECK(k[0].tick == 0 && k[1].tick == 10 && k[1].inTangent == 1.0f && k[1].outTangent == 2.0f);  // sorted by tick
  CHECK_EQ(pc[1].interp, kPropInterpStep);                                                       // Bool is always step
  CHECK_EQ(pc[1].componentType, kGameObjectTypeHash);
  const PackPropEvent* e = reinterpret_cast<const PackPropEvent*>(blob.data() + c.eventsOffset);
  CHECK(e[0].tick == 2 && strs[e[0].functionStr] == "A" && e[0].floatParameter == 1.5f && strs[e[0].stringStr] == "s");
  CHECK(e[1].tick == 8);
}

TEST(anim_cook_euler_angles_bake_to_a_rotation) {
  std::vector<u8> blob;
  AddStringFn add = [](const std::string&) { return 0u; };
  PackPropClip c;
  std::string err;
  const json doc = json::parse(R"({"format":1,"fps":30,"curves":[{"property":"Transform.localEulerAngles.z","keys":[{"t":0,"v":0},{"t":10,"v":90}]}]})");
  CHECK(bakePropClip(doc, Guid(), add, blob, c, err));
  CHECK_EQ(c.curveCount, 4u);  // x y z w of localRotation
  const PackPropCurve* pc = reinterpret_cast<const PackPropCurve*>(blob.data() + c.curvesOffset);
  CHECK_EQ(pc[2].fieldHash, fnv1a32("localRotation"));
  const PackPropKey* z = reinterpret_cast<const PackPropKey*>(blob.data() + pc[2].keysOffset);
  const PackPropKey* w = reinterpret_cast<const PackPropKey*>(blob.data() + pc[3].keysOffset);
  CHECK_EQ(pc[2].keyCount, 11u);  // one key per tick keeps nlerp close to the true arc
  CHECK_NEAR(z[10].value, 0.70710678f, 1e-5f);  // sin(45 deg)
  CHECK_NEAR(w[10].value, 0.70710678f, 1e-5f);
  CHECK_NEAR(z[0].value, 0.0f, 1e-6f);
}

TEST(anim_cook_rejects_bad_clips_with_a_reason) {
  AddStringFn add = [](const std::string&) { return 0u; };
  PackPropClip c;
  std::vector<u8> blob;
  std::string err;
  auto bad = [&](const char* text, const char* needle) {
    blob.clear();
    err.clear();
    const bool ok = bakePropClip(json::parse(text), Guid(), add, blob, c, err);
    if (ok || err.find(needle) == std::string::npos) { fprintf(stderr, "  expected '%s', got ok=%d '%s'\n", needle, ok, err.c_str()); CHECK(false); }
  };
  bad(R"({"format":2})", "format");
  bad(R"({"format":1,"fps":0})", "fps");
  bad(R"({"format":1,"curves":[{"property":"Nope.x.y","keys":[{"t":0,"v":0}]}]})", "unknown component type 'Nope'");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.color","keys":[{"t":0,"v":0}]}]})", "needs a channel");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.nothing","keys":[{"t":0,"v":0}]}]})", "no field path");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.srcX","keys":[]}]})", "no keys");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.srcX","interp":"wavy","keys":[{"t":0,"v":0}]}]})", "interp");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.srcX","keys":[{"t":3,"v":0},{"t":3,"v":1}]}]})", "two keys at tick 3");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.color.r","keys":[{"t":0,"v":300}]}]})", "0..255");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.srcX","keys":[{"t":9,"v":0}]}],"length":4})", "shorter than the last key");
  bad(R"({"format":1,"curves":[{"property":"SpriteRenderer.texture","keys":[{"t":0,"v":"nope"}]}]})", "guid");
  bad(R"({"format":1,"curves":[{"property":"Animator.controller.x","keys":[{"t":0,"v":0}]}]})", "no field path");
  bad(R"({"format":1,"events":[{"t":0}]})", "function");
}

TEST(anim_cook_controller_tables_and_errors) {
  std::vector<u8> blob;
  AddStringFn add = [](const std::string&) { return 0u; };
  MotionResolver mr;
  mr.resolve = [](const std::string& g, PackAnimStateKind& k, Guid& out, std::string& e) {
    if (g == "skel") { k = PackAnimStateKind::Clip3; return true; }
    if (g == "clip") { k = PackAnimStateKind::PropClip; return true; }
    e = "no such motion";
    (void)out;
    return false;
  };
  PackAnimController c;
  std::string err;
  const json doc = json::parse(R"({"format":1,"name":"C","defaultState":"B",
    "parameters":[{"name":"f","type":"float","default":0.5},{"name":"i","type":"int","default":3},{"name":"b","type":"bool"},{"name":"t","type":"trigger"}],
    "states":[{"name":"A","motion":"clip","speedParameter":"f"},{"name":"B","motion":"skel","loop":false,"speed":2},{"name":"E"}],
    "transitions":[{"from":"B","to":"A","hasExitTime":true,"exitTime":0.9,"duration":0.25,"conditions":[{"param":"b","mode":"ifNot"}]},
                   {"from":"*","to":"E","conditions":[{"param":"t"},{"param":"i","mode":"equals","value":4}]},
                   {"from":"A","to":"B","conditions":[{"param":"f","mode":"greater","value":0.1}]}]})");
  CHECK(bakeController(doc, Guid(), 60, mr, add, blob, c, err));
  CHECK_EQ(c.paramCount, 4u);
  CHECK_EQ(c.stateCount, 3u);
  CHECK_EQ(c.defaultState, 1u);
  CHECK_EQ(c.transitionCount, 3u);
  CHECK_EQ(c.anyCount, 1u);
  CHECK_EQ(c.anyFirst, 2u);
  const PackAnimState* st = reinterpret_cast<const PackAnimState*>(blob.data() + c.statesOffset);
  CHECK_EQ(st[0].kind, static_cast<u32>(PackAnimStateKind::PropClip));
  CHECK_EQ(st[1].kind, static_cast<u32>(PackAnimStateKind::Clip3));
  CHECK_EQ(st[2].kind, static_cast<u32>(PackAnimStateKind::Empty));
  CHECK_EQ(st[0].speedParam, 0u);
  CHECK((st[1].flags & kAnimStateLoop) == 0);
  CHECK(st[0].firstTransition == 0 && st[0].transitionCount == 1);  // A -> B
  CHECK(st[1].firstTransition == 1 && st[1].transitionCount == 1);  // B -> A
  const PackAnimTransition* ts = reinterpret_cast<const PackAnimTransition*>(blob.data() + c.transitionsOffset);
  CHECK_EQ(ts[1].durationTicks, 15u);  // 0.25 s at 60 Hz
  CHECK((ts[1].flags & kAnimTransHasExitTime) != 0);
  CHECK_EQ(ts[2].fromState, -1);
  const PackAnimParam* ps = reinterpret_cast<const PackAnimParam*>(blob.data() + c.paramsOffset);
  CHECK_EQ(ps[1].defaultBits, 3u);
  auto bad = [&](const char* text, const char* needle) {
    blob.clear();
    err.clear();
    const bool ok = bakeController(json::parse(text), Guid(), 60, mr, add, blob, c, err);
    if (ok || err.find(needle) == std::string::npos) { fprintf(stderr, "  expected '%s', got ok=%d '%s'\n", needle, ok, err.c_str()); CHECK(false); }
  };
  bad(R"({"format":1,"states":[]})", "at least one state");
  bad(R"({"format":1,"states":[{"name":"A"},{"name":"A"}]})", "unique");
  bad(R"({"format":1,"states":[{"name":"A","motion":"zzz"}]})", "no such motion");
  bad(R"({"format":1,"states":[{"name":"A"}],"transitions":[{"from":"A","to":"Q"}]})", "'Q'");
  bad(R"({"format":1,"states":[{"name":"A"}],"transitions":[{"from":"A","to":"A","conditions":[{"param":"x"}]}]})", "missing parameter 'x'");
  bad(R"({"format":1,"parameters":[{"name":"b","type":"bool"}],"states":[{"name":"A"}],"transitions":[{"from":"A","to":"A","conditions":[{"param":"b","mode":"greater"}]}]})", "use if / ifNot");
  bad(R"({"format":1,"parameters":[{"name":"f","type":"float"}],"states":[{"name":"A"}],"transitions":[{"from":"A","to":"A","conditions":[{"param":"f","mode":"if"}]}]})", "use greater");
  bad(R"({"format":1,"states":[{"name":"A"}],"defaultState":"Z"})", "defaultState");
  bad(R"({"format":1,"parameters":[{"name":"p","type":"quaternion"}],"states":[{"name":"A"}]})", "type must be");
}

TEST(anim_cook_project_cooks_identical_animation_sections_for_every_target_and_plays) {
  const fs::path dir = makeProject("rensga_anim_cook_ok", controller());
  std::vector<uint8_t> packs[2];
  int i = 0;
  for (const char* target : {"psp", "linux"}) {
    CookOptions o;
    o.project = dir;
    o.target = target;
    o.force = true;
    std::string firstError;
    CookResult r = cookProject(o, [&](const CookMessage& m) { if (m.severity == CookMessage::Error && firstError.empty()) firstError = m.text; });
    if (!r.ok) fprintf(stderr, "  cook %s failed: %s\n", target, firstError.c_str());
    CHECK(r.ok);
    packs[i++] = readAll(r.packPath);
  }
  Loaded a, b;
  CHECK(a.open(packs[0]) && b.open(packs[1]));
  const PackContent ca = a.view.content(), cb = b.view.content();
  CHECK_EQ(ca.propClipCount, 2u);
  CHECK_EQ(ca.controllerCount, 1u);
  CHECK_EQ(cb.propClipCount, 2u);
  // The animation records are the same bytes in every target's pack.
  // (blob offsets differ between targets because textures and meshes cook differently; the logical content is the same)
  CHECK(ca.controllers[0].guid == cb.controllers[0].guid);
  CHECK(ca.controllers[0].stateCount == cb.controllers[0].stateCount && ca.controllers[0].transitionCount == cb.controllers[0].transitionCount);
  CHECK(memcmp(ca.conditions(ca.controllers[0]), cb.conditions(cb.controllers[0]), ca.controllers[0].conditionCount * sizeof(PackAnimCondition)) == 0);
  CHECK(memcmp(ca.keys(ca.curves(ca.propClips[1])[0]), cb.keys(cb.curves(cb.propClips[1])[0]), 2 * sizeof(PackPropKey)) == 0);

  // Run the psp pack in a world.
  Arena arena;
  arena.create(1 << 21);
  World world;
  WorldConfig wc;
  wc.maxObjects = 16;
  wc.maxComponentsPerType = 8;
  world.init(wc, arena);
  LoadedPack lp;
  CHECK(world.loadPack(a.view, &lp));
  PackAssets assets = a.view.assets();
  GfxAssets::bind(&a.view, &assets, nullptr, 0);
  GameLoop loop;
  loop.init(world);
  VirtualGamepad pad;
  GameObject* hero = world.findByName("Hero");
  Animator* an = hero->GetComponent<Animator>();
  SpriteRenderer* sr = hero->GetComponent<SpriteRenderer>();
  for (int f = 0; f < 31; ++f) loop.frame(1.0f / 60.0f, pad);
  CHECK(an->GetCurrentStateInfo().IsName("Idle"));
  CHECK_EQ(sr->srcX, 16);  // Idle key at tick 30 (60 fps = 1 tick per step)
  an->SetFloat("speed", 1.0f);
  for (int f = 0; f < 3; ++f) loop.frame(1.0f / 60.0f, pad);  // 0.1 s cross-fade = 6 steps
  AnimatorSystem::DebugInfo d;
  CHECK(AnimatorSystem::debugInfo(*an, d) && d.next == 1u);
  for (int f = 0; f < 4; ++f) loop.frame(1.0f / 60.0f, pad);
  CHECK(an->GetCurrentStateInfo().IsName("Run"));
  CHECK(sr->srcX == 100 || sr->srcX == 116);
  CHECK(hero->transform.localPosition().x > 0.0f);
  CHECK(sr->texture.bytes[15] == 0x01 && sr->texture.bytes[0] == 0x00);
  AnimationSystem::shutdown();
  GfxAssets::unbind();
  world.shutdown();
  fs::remove_all(dir);
}

TEST(anim_cook_project_errors_name_the_asset) {
  json bad = controller();
  bad["states"][0]["motion"] = "00000000-0000-4000-8000-00000000dead";
  fs::path dir = makeProject("rensga_anim_cook_bad", bad);
  CookOptions o;
  o.project = dir;
  o.target = "psp";
  o.force = true;
  std::string asset, text;
  CookResult r = cookProject(o, [&](const CookMessage& m) { if (m.severity == CookMessage::Error && asset.empty()) { asset = m.asset; text = m.text; } });
  CHECK(!r.ok);
  CHECK(asset == "Assets/Anim/Player.controller.json");
  CHECK(text.find("state 'Idle'") != std::string::npos);
  fs::remove_all(dir);

  dir = makeProject("rensga_anim_cook_badref", controller(), true);
  o.project = dir;
  asset.clear();
  text.clear();
  r = cookProject(o, [&](const CookMessage& m) { if (m.severity == CookMessage::Error && asset.empty()) { asset = m.asset; text = m.text; } });
  CHECK(!r.ok);
  CHECK(text.find("Animator.controller") != std::string::npos && text.find("Hero") != std::string::npos);
  fs::remove_all(dir);
}

TEST(anim_cook_projects_without_animation_assets_get_no_animation_sections) {
  const fs::path dir = makeProject("rensga_anim_cook_none", json::object(), false, false);
  CookOptions o;
  o.project = dir;
  o.target = "psp";
  o.force = true;
  std::string firstError;
  CookResult r = cookProject(o, [&](const CookMessage& m) { if (m.severity == CookMessage::Error && firstError.empty()) firstError = m.text; });
  if (!r.ok) fprintf(stderr, "  cook failed: %s\n", firstError.c_str());
  CHECK(r.ok);
  Loaded l;
  CHECK(l.open(readAll(r.packPath)));
  CHECK(l.view.find(pack_section::PCLP) == nullptr && l.view.find(pack_section::ANIC) == nullptr);
  fs::remove_all(dir);
}
