// F6.03 (Editor 9): the clip and controller documents and the preview / record model behind the Animation window and the Animator graph
// (editor/core, no UI): edits and undo, the bake the preview and the cooker share, scrubbing a scene, recording Inspector and Scene
// view edits as keys, and that ending the preview leaves the document exactly as it was.
#include <filesystem>
#include <fstream>

#include "rensga/core/components.h"
#include "rensga/editor/anim_clip_doc.h"
#include "rensga/editor/anim_controller_doc.h"
#include "rensga/editor/anim_preview.h"
#include "rensga/editor/scene_document.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
fs::path tmpDir(const char* name) {
  const fs::path d = fs::temp_directory_path() / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
void put(const fs::path& p, const std::string& s) {
  std::ofstream f(p, std::ios::binary);
  f << s;
}
const char* kClip = R"({"format":1,"kind":"PropertyClip","name":"Run","fps":12,"length":4,"loop":true,
  "curves":[{"property":"SpriteRenderer.srcX","interp":"step","keys":[{"t":0,"v":0},{"t":1,"v":32},{"t":2,"v":64}]},
            {"property":"Transform.localPosition.x","keys":[{"t":0,"v":0},{"t":4,"v":8}]}],"events":[{"t":2,"function":"Footstep","float":0.7}]})";
const char* kScene = R"({"format":1,"guid":"00000000-0000-4000-a000-0000000000f1","objects":[
  {"guid":"00000000-0000-4000-9000-000000000001","name":"Player","parent":null,"active":true,
   "transform":{"position":[10,20,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
   "components":[{"type":"SpriteRenderer","fields":{"srcX":5}}],"prefab":null},
  {"guid":"00000000-0000-4000-9000-000000000002","name":"Arm","parent":"00000000-0000-4000-9000-000000000001","active":true,
   "transform":{"position":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":[],"prefab":null}]})";
const char* kGuid = "00000000-0000-4000-9000-000000000001";
}  // namespace

TEST(anim_editor_clip_document_edits_undo_and_roundtrip) {
  const fs::path d = tmpDir("rensga_anim_editor_clip");
  put(d / "Run.clip.json", kClip);
  AnimClipDoc c;
  std::string err;
  CHECK(c.load(d / "Run.clip.json", err));
  CHECK_EQ(c.fps(), 12u);
  CHECK_EQ(c.curves().size(), 2u);
  CHECK_EQ(c.events().size(), 1u);
  CHECK(!c.dirty());
  const int ci = c.addCurve("Arm", "Transform.localScale.y");
  CHECK(ci == 2 && c.addCurve("Arm", "Transform.localScale.y") == 2);        // no duplicate
  CHECK(c.addCurve("", "SpriteRenderer.nothing") < 0);                       // not a field
  CHECK(c.setKey(ci, 3, 2.0) && c.setKey(ci, 1, 1.0));
  CHECK_EQ(c.curves()[ci].keys[0].tick, 1u);                                 // kept sorted
  CHECK(!c.moveKey(ci, 0, 3));                                               // another key is there
  CHECK(c.moveKey(ci, 0, 2) && c.curves()[ci].keys[0].tick == 2);
  CHECK(c.setKey(ci, 9, 5.0) && c.length() == 9);                            // the clip grows with its keys
  CHECK(c.dirty());
  CHECK(c.undo() && c.length() == 4 && c.curves()[ci].keys.size() == 2);
  CHECK(c.redo() && c.length() == 9);
  CHECK(c.undo() && c.undo() && c.undo() && c.undo() && c.undo());   // setKey 9, moveKey, two setKeys, addCurve
  CHECK_EQ(c.curves().size(), 2u);                                           // back to the loaded state
  CHECK(c.save(err));
  AnimClipDoc again;
  CHECK(again.load(d / "Run.clip.json", err));
  CHECK_EQ(again.toJson().dump(), c.toJson().dump());
  // evalCurve follows the runtime rules
  CHECK_NEAR(AnimClipDoc::evalCurve(c.curves()[1], 2.0), 4.0, 1e-9);
  CHECK_NEAR(AnimClipDoc::evalCurve(c.curves()[0], 1.9), 32.0, 1e-9);        // step holds the left key
  BakedClip b;
  CHECK(c.bake(b, err));
  CHECK_EQ(b.rec.curveCount, 2u);
  CHECK_NEAR(PropClips::sampleCurve(b.source(), b.source().curves()[1], static_cast<u64>(2.0 * 65536)), 4.0f, 1e-5f);
  fs::remove_all(d);
}

TEST(anim_editor_property_menu_comes_from_reflection) {
  const auto props = animatableProperties({"SpriteRenderer", "Animator"});
  bool transform = false, active = false, color = false, srcX = false, controller = false;
  for (const AnimComponentProps& cp : props)
    for (const AnimProperty& p : cp.properties) {
      transform = transform || p.property == "Transform.localPosition.x";
      active = active || p.property == "GameObject.active";
      color = color || p.property == "SpriteRenderer.color.g";
      srcX = srcX || p.property == "SpriteRenderer.srcX";
      controller = controller || p.property == "Animator.controller";
    }
  CHECK(transform && active && color && srcX && controller);
  CHECK(animValueKind("SpriteRenderer.color.r") == AnimValueKind::Color && animValueKind("SpriteRenderer.srcX") == AnimValueKind::Int);
  CHECK(animValueKind("SpriteRenderer.texture") == AnimValueKind::Guid && animValueKind("GameObject.active") == AnimValueKind::Bool);
  CHECK(animValueKind("Nope.x") == AnimValueKind::Unknown);
}

TEST(anim_editor_preview_scrubs_records_and_leaves_the_document_alone) {
  const fs::path d = tmpDir("rensga_anim_editor_preview");
  put(d / "Run.clip.json", kClip);
  put(d / "Main.scene", kScene);
  AnimClipDoc clip;
  std::string err;
  CHECK(clip.load(d / "Run.clip.json", err));
  SceneDocument doc;
  CHECK(doc.load(d / "Main.scene", err));
  const std::string before = doc.document().dump();
  AnimPreview pv;
  CHECK(pv.begin(doc, kGuid, clip, err));
  CHECK(doc.playing());                                                      // the document is frozen behind the mirror
  pv.setTime(2.0);
  double v = -1;
  CHECK(pv.value("", "SpriteRenderer.srcX", &v) && v == 64.0);
  CHECK(pv.value("", "Transform.localPosition.x", &v) && v == 4.0);
  CHECK(pv.value("Arm", "Transform.localPosition.x", &v) && v == 1.0);        // not animated
  pv.setTime(0.0);
  CHECK(pv.value("", "SpriteRenderer.srcX", &v) && v == 0.0);
  CHECK(doc.scene().objects[0].position[0] == 0.0f);                          // the Scene view reads the mirror
  // without record an edit only changes the preview
  const float pos[3] = {50, 0, 0}, rot[4] = {0, 0, 0, 1}, scl[3] = {1, 1, 1};
  CHECK(doc.setTransform(kGuid, pos, rot, scl));
  CHECK(clip.curves().size() == 2 && pv.keysRecorded() == 0);
  pv.setTime(4.0);
  CHECK(pv.value("", "Transform.localPosition.x", &v) && v == 8.0);          // the clip owns the animated property again
  // record: a field edit and a transform edit on a child become keys at the playhead
  pv.setRecord(true);
  pv.setTime(3.0);
  CHECK(doc.setField(kGuid, 0, "srcX", 77));
  CHECK(pv.keysRecorded() == 1 && clip.findCurve("", "SpriteRenderer.srcX") == 0);
  CHECK(clip.hasKeyAt(0, 3));
  const float cpos[3] = {2, 7, 0};
  CHECK(doc.setTransform("00000000-0000-4000-9000-000000000002", cpos, rot, scl));
  const int armY = clip.findCurve("Arm", "Transform.localPosition.y");
  CHECK(armY >= 0 && clip.hasKeyAt(armY, 3));
  CHECK(clip.findCurve("Arm", "Transform.localPosition.x") >= 0);
  pv.setTime(3.0);
  CHECK(pv.value("", "SpriteRenderer.srcX", &v) && v == 77.0);
  CHECK(pv.value("Arm", "Transform.localPosition.y", &v) && v == 7.0);
  // anything outside the animated object cannot be keyed
  pv.end(doc);
  CHECK(!doc.playing());
  CHECK_EQ(doc.document().dump(), before);                                    // the document never changed
  CHECK(!doc.dirty() && doc.undoDepth() == 0);
  fs::remove_all(d);
}

TEST(anim_editor_controller_document_edits_validates_and_roundtrips) {
  const fs::path d = tmpDir("rensga_anim_editor_ctl");
  AnimControllerDoc c;
  std::string err;
  put(d / "P.controller.json", R"({"format":1,"kind":"AnimatorController","name":"P","parameters":[],"defaultState":"Idle","states":[{"name":"Idle","motion":"","loop":true,"pos":[10,20]}],"transitions":[]})");
  CHECK(c.load(d / "P.controller.json", err));
  CHECK(c.addState("Run", 200, 20) == 1 && c.addState("Run", 0, 0) < 0 && c.addState("", 0, 0) < 0);
  CHECK(c.addParam("speed", AnimParamType::Float) == 0 && c.addParam("speed", AnimParamType::Int) < 0);
  CHECK(c.addParam("jump", AnimParamType::Trigger) == 1);
  const int t = c.addTransition("Idle", "Run");
  CHECK(t == 0 && c.addTransition("Idle", "Nope") < 0);
  AnimCondDef cd;
  cd.param = "speed";
  cd.mode = "greater";
  cd.value = 0.1;
  CHECK(c.addCondition(t, cd));
  cd.param = "missing";
  CHECK(!c.addCondition(t, cd));
  CHECK(c.setTransition(t, true, 0.9, 0.2) && !c.setTransition(t, false, 1, 100));
  CHECK(c.addTransition("*", "Idle") == 1);
  CHECK(c.validate().empty());
  CHECK(c.renameState("Run", "Sprint") && c.transitions()[0].to == "Sprint");
  CHECK(c.setStateOptions("Sprint", false, 2.0, "speed") && !c.setStateOptions("Sprint", true, 1.0, "jump"));  // a trigger is no speed
  CHECK(c.removeParam("speed"));                                                // its condition and speedParameter go with it
  CHECK(c.transitions()[0].conditions.empty() && c.states()[1].speedParameter.empty());
  CHECK(c.undo());
  CHECK(c.transitions()[0].conditions.size() == 1 && c.params().size() == 2);
  CHECK(c.removeState("Idle") && c.defaultState() == "Sprint" && c.transitions().empty());
  CHECK(c.save(err));
  AnimControllerDoc again;
  CHECK(again.load(d / "P.controller.json", err));
  CHECK_EQ(again.toJson().dump(), c.toJson().dump());
  CHECK(c.addState("Idle", 0, 0) >= 0);
  c.addTransition("Idle", "Sprint");
  CHECK(c.addCondition(0, [] { AnimCondDef x; x.param = "jump"; x.mode = "greater"; return x; }()));
  CHECK(!c.validate().empty());                                                // a trigger cannot use greater
  fs::remove_all(d);
}
