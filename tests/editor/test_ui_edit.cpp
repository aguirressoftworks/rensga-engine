// F6.04 Editor 10 (D1): creating UI objects, the headless UI preview, rect math, the Input Actions document and the Physics2D document.
#include <filesystem>
#include <fstream>

#include "rensga/editor/input_actions_doc.h"
#include "rensga/editor/physics2d_settings.h"
#include "rensga/editor/scene_document.h"
#include "rensga/editor/ui_edit.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
fs::path fresh(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "ProjectSettings");
  return d;
}
bool newScene(SceneDocument& doc, const fs::path& dir) {
  fs::path f = dir / "T.scene";
  std::ofstream(f) << R"({"format":1,"guid":"00000000-0000-4000-a000-000000000001","objects":[]})";
  std::string err;
  return doc.load(f, err);
}
const SceneObject* byName(const SceneDocument& d, const char* n) {
  for (const SceneObject& o : d.scene().objects) if (o.name == n) return &o;
  return nullptr;
}
int countType(const SceneDocument& d, const char* t) {
  int n = 0;
  for (const SceneObject& o : d.scene().objects) for (const SceneComponent& c : o.components) if (c.type == t) ++n;
  return n;
}
}  // namespace

TEST(ui_edit_create_menu_objects_and_undo) {
  SceneDocument doc;
  CHECK(newScene(doc, fresh("ui_create")));
  const std::string btn = createUiObject(doc, UiKind::Button, "", 480, 272);
  CHECK(!btn.empty());
  CHECK_EQ(countType(doc, "Canvas"), 1);        // a Canvas is made first
  CHECK_EQ(countType(doc, "EventSystem"), 1);   // an interactive element gets an EventSystem
  CHECK_EQ(countType(doc, "UIButton"), 1);
  CHECK_EQ(countType(doc, "UIText"), 1);        // the Label
  const SceneObject* canvas = byName(doc, "Canvas");
  CHECK(canvas && canvas->components[0].fields["referenceWidth"] == 480 && canvas->components[0].fields["scaleMode"] == 1);
  CHECK(doc.find(btn)->parentGuid == canvas->guid);
  const std::string sl = createUiObject(doc, UiKind::Slider, "", 480, 272);
  const SceneObject* s = doc.find(sl);
  CHECK(s && s->components.size() == 2 && s->components[1].type == "Slider");
  CHECK(s->components[1].fields["fillRect"].is_string() && !s->components[1].fields["fillRect"].get<std::string>().empty());
  CHECK(s->components[1].fields["handleRect"].is_string() && !s->components[1].fields["handleRect"].get<std::string>().empty());
  CHECK_EQ(countType(doc, "Canvas"), 1);        // not duplicated
  CHECK_EQ(countType(doc, "EventSystem"), 1);
  const size_t before = doc.scene().objects.size();
  CHECK(doc.undo());                            // one undo step removes the slider and its two children
  CHECK_EQ(doc.scene().objects.size(), before - 3);
  const std::string tg = createUiObject(doc, UiKind::Toggle, "", 480, 272);
  const SceneObject* t = doc.find(tg);
  CHECK(t && t->components[1].fields["graphic"].is_string());
  CHECK(!createUiObject(doc, UiKind::Text, "", 480, 272).empty());
  CHECK(!createUiObject(doc, UiKind::Image, "", 480, 272).empty());
  CHECK(!createUiObject(doc, UiKind::LayoutGroup, "", 480, 272).empty());
  UiKind k;
  CHECK(uiKindFromName("Layout_Group", k) && k == UiKind::LayoutGroup);
  CHECK(!uiKindFromName("nope", k));
}

TEST(ui_edit_preview_scales_to_the_target_and_lays_out) {
  SceneDocument doc;
  CHECK(newScene(doc, fresh("ui_preview")));
  const std::string a = createUiObject(doc, UiKind::Button, "", 480, 272);
  CHECK(!a.empty());
  UiPreview pv;
  CHECK(pv.rebuild(doc, 480, 272));
  CHECK_NEAR(pv.view().scale, 1.0f, 1e-5f);
  const UiElement* e = pv.find(a);
  CHECK(e != nullptr);
  CHECK(e && e->kind == UiElement::Button);
  // A centred 120 x 24 button in a 480 x 272 view.
  CHECK(e && e->vx == 180.0f && e->vw == 120.0f && e->vh == 24.0f);
  CHECK(pv.rebuild(doc, 640, 448));              // ScaleWithScreenSize 480 x 272 -> 640 x 448, match 0.5
  const float s = pv.view().scale;
  CHECK_NEAR(s, (640.0f / 480.0f + 448.0f / 272.0f) * 0.5f, 1e-3f);
  const UiElement* e2 = pv.find(a);
  CHECK(e2 && e2->w > e2->vw);                   // drawn bigger than its virtual size
  // The label child shows the text typed in the editor.
  bool label = false;
  for (const UiElement& el : pv.elements()) if (el.kind == UiElement::Text && el.text == "Button") label = true;
  CHECK(label);
  // Slider: the fill follows the value and is marked as driven.
  const std::string sl = createUiObject(doc, UiKind::Slider, "", 480, 272);
  CHECK(pv.rebuild(doc, 480, 272));
  const SceneObject* so = doc.find(sl);
  const std::string fillGuid = so->components[1].fields["fillRect"].get<std::string>();
  const UiElement* fill = pv.find(fillGuid);
  CHECK(fill && fill->driven && fill->vw == 60.0f);   // half of the 120 px track
  doc.setField(sl, 1, "value", 1.0);
  CHECK(pv.rebuild(doc, 480, 272));
  CHECK(pv.find(fillGuid)->vw == 120.0f);
  // Layout group: children are placed by it.
  const std::string lg = createUiObject(doc, UiKind::LayoutGroup, "", 480, 272);
  const std::string c1 = doc.createObject("c1", lg, {SceneComponent{"UIImage", json::object({{"w", 40}, {"h", 20}})}});
  const std::string c2 = doc.createObject("c2", lg, {SceneComponent{"UIImage", json::object({{"w", 40}, {"h", 20}})}});
  CHECK(pv.rebuild(doc, 480, 272));
  const UiElement *e1 = pv.find(c1), *e2b = pv.find(c2);
  CHECK(e1 && e2b && e1->driven && e1->vy < e2b->vy && e1->vx == e2b->vx);   // a vertical column
}

TEST(ui_edit_rect_math_round_trips) {
  for (int anchor = 0; anchor < 9; ++anchor) {
    int x, y;
    uiPlaceTopLeft(anchor, 37, 21, 50, 14, 480, 272, x, y);
    int r[4];
    uiResolve(anchor, x, y, 50, 14, 480, 272, r);
    CHECK(r[0] == 37 && r[1] == 21 && r[2] == 50 && r[3] == 14);
  }
}

TEST(ui_edit_input_actions_round_trip_validate_and_edit) {
  fs::path d = fresh("input_actions");
  InputActionsDoc doc;
  std::string err;
  CHECK(loadInputActions(d, doc, err) && doc.actions.empty());   // a missing file is no actions
  int jump = doc.add("button");
  CHECK(doc.rename(jump, "Jump"));
  doc.actions[0].alt = {"key:Space"};
  int move = doc.add("axis2");
  CHECK(doc.rename(move, "Move"));
  doc.actions[1].source = "composite";
  doc.actions[1].up = {"key:W"}; doc.actions[1].down = {"key:S"}; doc.actions[1].left = {"key:A"}; doc.actions[1].right = {"key:D"};
  doc.actions[1].snap = true;
  int look = doc.add("axis");
  doc.actions[2].name = "Look";
  doc.actions[2].source = "rightx";
  doc.actions[2].dead = 0.1;
  CHECK(!doc.rename(look, "Jump"));               // taken
  CHECK(!doc.rename(look, ""));
  CHECK(doc.validate().empty());
  CHECK(saveInputActions(d, doc, err));
  InputActionsDoc back;
  CHECK(loadInputActions(d, back, err));
  CHECK(back.toJson() == doc.toJson());
  CHECK(back.actions[0].alt.size() == 1 && back.actions[1].snap && back.actions[2].dead == 0.1);
  // The cooker is the judge: an unknown binding is reported.
  back.actions[0].alt = {"key:NoSuchKey"};
  CHECK(!back.validate().empty());
  back.actions[0].alt.clear();
  back.actions.push_back(back.actions[0]);
  bool dup = false;
  for (const std::string& e : back.validate()) dup = dup || e.find("twice") != std::string::npos;
  CHECK(dup);
  back.actions.pop_back();
  CHECK(back.move(0, 2) && back.actions[2].name == "Jump");
  CHECK(back.setKind(2, "axis") && back.actions[2].source == "leftx" && back.actions[2].alt.empty());
  CHECK(back.remove(2) && back.actions.size() == 2);
  // An existing project's file survives a load / save round trip byte for byte in meaning.
  json src = {{"format", 1}, {"stickDeadZone", 0.2}, {"stickOuterClamp", 0.95}, {"custom", 5},
              {"actions", json::array({json::object({{"name", "Fire"}, {"kind", "button"}, {"source", "west"}, {"alt", "key:LeftControl"}, {"note", "x"}})})}};
  InputActionsDoc keep;
  CHECK(keep.fromJson(src, err));
  CHECK(keep.toJson() == src);                      // unknown keys kept too
  CHECK(inputBindingLabel("south") == "Cross (south)");
  CHECK(inputBindingLabel("key:Space") == "Key Space");
  CHECK(inputKeyNames().size() > 60);
}

TEST(ui_edit_physics2d_matrix) {
  fs::path d = fresh("phys2d");
  std::ofstream(d / "ProjectSettings" / "Physics2D.json") << R"({"format":1,"layers":["Default","Player","Enemy","Pickup"],"ignore":[["Enemy","Pickup"]]})";
  Physics2DSettings p;
  std::string err;
  CHECK(loadPhysics2D(d, p, err));
  CHECK_EQ(p.layers.size(), 4u);
  CHECK(p.collides(0, 3));
  CHECK(!p.collides(2, 3) && !p.collides(3, 2));
  p.setCollides(1, 2, false);
  p.setCollides(2, 3, true);
  CHECK(!p.collides(1, 2) && p.collides(2, 3));
  CHECK(p.validate().empty());
  CHECK(savePhysics2D(d, p, err));
  Physics2DSettings q;
  CHECK(loadPhysics2D(d, q, err));
  CHECK(!q.collides(2, 1) && q.collides(3, 2));
  CHECK_EQ(q.ignore.size(), 1u);
  Physics2DSettings none;
  CHECK(loadPhysics2D(fresh("phys2d_missing"), none, err) && none.layers.size() == 1 && none.collides(0, 0));
}
