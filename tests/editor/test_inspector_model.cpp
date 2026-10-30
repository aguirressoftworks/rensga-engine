// F3.01 / F3.07 / gate F3.G2: the inspector model without ImGui. Every edit path must undo and redo to the exact document JSON.
#include <filesystem>
#include <functional>

#include "rensga/core/object.h"
#include "rensga/core/unity_event.h"
#include "rensga/editor/inspector_model.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace rensga {
namespace insptest {

struct InspProbe : Behaviour {
  REFLECT(InspProbe)
  EXECUTE_IN_EDIT_MODE()
  HEADER("Numbers")
  TOOLTIP("How strong")
  RANGE(0, 10)
  SERIALIZE_FIELD(Scalar, level, 1)
  SERIALIZE_FIELD(i32, count, 2)
  SERIALIZE_FIELD(bool, flag, false)
  SPACE(12)
  SERIALIZE_FIELD(Vector3, pos, Vector3(0, 0, 0))
  SERIALIZE_FIELD(Quaternion, rot, Quaternion())
  SERIALIZE_FIELD(Color32, tint, Color32(255, 255, 255, 255))
  SERIALIZE_FIELD(Guid, target, Guid())
  SERIALIZE_FIELD(UnityEvent, onDone, {})
  HIDE_IN_INSPECTOR()
  SERIALIZE_FIELD(i32, secret, 7)
  CONTEXT_MENU("Fill Count")
  void Fill() { count = 9; }
  void OnValidate() override {
    if (level > 10) level = 10;  // clamp: an inspector edit past the range is corrected in the same undo step
    if (count < 0) count = 0;
  }
  void Reset() override { count = 5; }
};

struct InspNeeds : Behaviour {
  REFLECT(InspNeeds)
  REQUIRE_COMPONENT(InspProbe)
  SERIALIZE_FIELD(i32, n, 0)
};

}  // namespace insptest
}  // namespace rensga

static fs::path templateScene() { return fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene"; }

struct Fixture {
  SceneDocument d;
  std::string a, b;
  Fixture() {
    std::string err;
    d.load(templateScene(), err);
    a = d.createObject("A", "");
    b = d.createObject("B", "");
    d.addComponent(a, "InspProbe", typeDefaults(*TypeRegistry::instance().find("InspProbe")));
    d.addComponent(b, "InspProbe", typeDefaults(*TypeRegistry::instance().find("InspProbe")));
    d.select(a);
  }
};

// Runs one edit, then checks it is exactly one undo step and that undo/redo restore the JSON bit for bit.
static void g2(Fixture& f, const char* name, const std::function<bool()>& edit) {
  const nlohmann::ordered_json before = f.d.document();
  const size_t depth = f.d.undoDepth();
  bool ok = edit();
  if (!ok) { fprintf(stderr, "  G2 edit failed: %s\n", name); CHECK(ok); return; }
  const nlohmann::ordered_json after = f.d.document();
  if (after == before) { fprintf(stderr, "  G2 edit changed nothing: %s\n", name); CHECK(after != before); return; }
  if (f.d.undoDepth() != depth + 1) fprintf(stderr, "  G2 not one undo step: %s (%zu -> %zu)\n", name, depth, f.d.undoDepth());
  CHECK_EQ(f.d.undoDepth(), depth + 1);
  CHECK(f.d.undo());
  if (f.d.document() != before) fprintf(stderr, "  G2 undo not exact: %s\n", name);
  CHECK(f.d.document() == before);
  CHECK(f.d.redo());
  if (f.d.document() != after) fprintf(stderr, "  G2 redo not exact: %s\n", name);
  CHECK(f.d.document() == after);
}

static ComponentRef cref(const std::string& g, int i = 0) { return ComponentRef{g, i}; }

TEST(insp_attributes_are_in_the_type_table) {
  const TypeInfo* t = TypeRegistry::instance().find("InspProbe");
  CHECK(t != nullptr);
  CHECK(t->executeInEditMode);
  CHECK_EQ(t->contextMenuCount, 1u);
  CHECK(strcmp(t->contextMenus[0].label, "Fill Count") == 0 && strcmp(t->contextMenus[0].method, "Fill") == 0);
  CHECK(findMethod(*t, "Fill") != nullptr);
  const FieldDesc& level = t->fields[0];
  CHECK(level.hasRange && strcmp(level.header, "Numbers") == 0 && strcmp(level.tooltip, "How strong") == 0);
  CHECK_EQ(t->fields[3].space, 12u);
  const FieldDesc& secret = t->fields[t->fieldCount - 1];
  CHECK(strcmp(secret.name, "secret") == 0 && secret.hideInInspector);
  CHECK(!level.hideInInspector);
  const TypeInfo* n = TypeRegistry::instance().find("InspNeeds");
  CHECK(n && n->requireCount == 1 && strcmp(n->requireNames[0], "InspProbe") == 0);
}

TEST(insp_g2_every_field_type_edit_undoes) {
  Fixture f;
  g2(f, "scalar", [&] { return bool(editFields(f.d, {cref(f.a)}, "level", 4.5)); });
  g2(f, "i32", [&] { return bool(editFields(f.d, {cref(f.a)}, "count", 11)); });
  g2(f, "bool", [&] { return bool(editFields(f.d, {cref(f.a)}, "flag", true)); });
  g2(f, "vector3", [&] { return bool(editFields(f.d, {cref(f.a)}, "pos", json::array({1.5, -2, 3}))); });
  g2(f, "quaternion", [&] { return bool(editFields(f.d, {cref(f.a)}, "rot", json::array({0, 0.5, 0, 0.8660254}))); });
  g2(f, "color32", [&] { return bool(editFields(f.d, {cref(f.a)}, "tint", json::array({10, 20, 30, 255}))); });
  g2(f, "guid object ref", [&] { return bool(editFields(f.d, {cref(f.a)}, "target", f.b)); });
  g2(f, "guid asset ref drop", [&] { return bool(editFields(f.d, {cref(f.a)}, "target", "12345678-1234-4234-8234-123456789abc")); });
  json ev = json::array({json{{"target", f.b}, {"type", "InspProbe"}, {"method", "Fill"}}});
  g2(f, "unity event listener", [&] { return bool(editFields(f.d, {cref(f.a)}, "onDone", ev)); });
  json ev2 = ev;
  ev2.push_back(json{{"target", f.a}, {"type", "InspProbe"}, {"method", "Fill"}});
  g2(f, "unity event second listener", [&] { return bool(editFields(f.d, {cref(f.a)}, "onDone", ev2)); });
}

TEST(insp_g2_object_edits_undo) {
  Fixture f;
  g2(f, "name", [&] { return bool(editObjects(f.d, {f.a}, "name", "Renamed")); });
  g2(f, "active", [&] { return bool(editObjects(f.d, {f.a}, "active", false)); });
  g2(f, "tag", [&] { return bool(editObjects(f.d, {f.a}, "tag", "Player")); });
  g2(f, "layer", [&] { return bool(editObjects(f.d, {f.a}, "layer", 3)); });
  g2(f, "multi name", [&] { return bool(editObjects(f.d, {f.a, f.b}, "name", "Both")); });
}

TEST(insp_g2_component_menu_undoes) {
  Fixture f;
  const TypeInfo* probe = TypeRegistry::instance().find("InspProbe");
  (void)probe;
  g2(f, "add component", [&] { return bool(addComponentTo(f.d, f.a, "InspProbe")); });
  g2(f, "add with required component", [&] {
    OpResult r = addComponentTo(f.d, f.b, "InspNeeds");
    return bool(r);
  });
  // b now has InspProbe, InspNeeds: the requirement was not added twice
  CHECK_EQ(f.d.find(f.b)->components.size(), size_t(2));
  CHECK(f.d.find(f.b)->components[1].type == "InspNeeds");
  g2(f, "move down", [&] { return bool(moveComponentBy(f.d, f.a, 0, 1)); });
  g2(f, "move up", [&] { return bool(moveComponentBy(f.d, f.a, 1, -1)); });
  g2(f, "edit then reset", [&] { return bool(editFields(f.d, {cref(f.a)}, "level", 7)); });
  g2(f, "reset", [&] { return bool(resetComponent(f.d, f.a, 0)); });
  CHECK_EQ(fieldValue(f.d, cref(f.a), "count").get<int>(), 5);  // Reset() ran
  CHECK_EQ(fieldValue(f.d, cref(f.a), "level").get<double>(), 1.0);
  // copy / paste values
  editFields(f.d, {cref(f.a)}, "level", 6);
  editFields(f.d, {cref(f.a)}, "pos", json::array({1, 2, 3}));
  CHECK(copyComponentValues(f.d, f.a, 0));
  g2(f, "paste values", [&] { return bool(pasteComponentValues(f.d, f.b, 0)); });
  CHECK(fieldValue(f.d, cref(f.b), "pos") == json::array({1, 2, 3}));
  // wrong type is refused and changes nothing
  size_t depth = f.d.undoDepth();
  CHECK(!pasteComponentValues(f.d, f.b, 1));
  CHECK_EQ(f.d.undoDepth(), depth);
  g2(f, "context menu", [&] { return bool(runContextMenu(f.d, f.a, 0, "Fill")); });
  CHECK_EQ(fieldValue(f.d, cref(f.a), "count").get<int>(), 9);
  g2(f, "remove component", [&] { return bool(removeComponentFrom(f.d, f.a, 1)); });
}

TEST(insp_g2_on_validate_is_part_of_the_edit_step) {
  Fixture f;
  const nlohmann::ordered_json before = f.d.document();
  size_t depth = f.d.undoDepth();
  CHECK(editFields(f.d, {cref(f.a)}, "level", 50));  // OnValidate clamps to 10 in the same step
  CHECK_EQ(fieldValue(f.d, cref(f.a), "level").get<double>(), 10.0);
  CHECK_EQ(f.d.undoDepth(), depth + 1);
  nlohmann::ordered_json after = f.d.document();
  CHECK(f.d.undo());
  CHECK(f.d.document() == before);
  CHECK(f.d.redo());
  CHECK(f.d.document() == after);
  CHECK(editFields(f.d, {cref(f.a)}, "count", -4));
  CHECK_EQ(fieldValue(f.d, cref(f.a), "count").get<int>(), 0);
  // a second edit with no side effect is a clean single step
  depth = f.d.undoDepth();
  CHECK(editFields(f.d, {cref(f.a)}, "flag", true));
  CHECK_EQ(f.d.undoDepth(), depth + 1);
}

TEST(insp_g2_multi_edit_one_step_and_mixed_values) {
  Fixture f;
  CHECK(editFields(f.d, {cref(f.a)}, "level", 2));
  CHECK(editFields(f.d, {cref(f.b)}, "level", 3));
  std::vector<std::string> sel = {f.a, f.b};
  CHECK(commonComponentTypes(f.d, sel) == std::vector<std::string>{"InspProbe"});
  std::vector<ComponentRef> refs = sameTypeAcross(f.d, sel, "InspProbe");
  CHECK_EQ(refs.size(), size_t(2));
  CHECK(fieldMixed(f.d, refs, "level"));
  CHECK(!fieldMixed(f.d, refs, "count"));
  g2(f, "multi edit", [&] { return bool(editFields(f.d, refs, "level", 4)); });
  CHECK(!fieldMixed(f.d, refs, "level"));
  g2(f, "multi clamp", [&] { return bool(editFields(f.d, refs, "level", 99)); });
  CHECK_EQ(fieldValue(f.d, refs[1], "level").get<double>(), 10.0);
  // an object without the component makes the type not common
  std::string c = f.d.createObject("C", "");
  CHECK(sameTypeAcross(f.d, {f.a, c}, "InspProbe").empty());
  CHECK(commonComponentTypes(f.d, {f.a, c}).empty());
}

TEST(insp_requirements_and_remove_refusal) {
  Fixture f;
  CHECK(addComponentTo(f.d, f.a, "InspNeeds"));
  const SceneObject* o = f.d.find(f.a);
  CHECK_EQ(o->components.size(), size_t(2));
  OpResult r = removeComponentFrom(f.d, f.a, 0);  // InspProbe is required by InspNeeds
  CHECK(!r);
  CHECK(r.reason.find("InspNeeds") != std::string::npos);
  CHECK_EQ(f.d.find(f.a)->components.size(), size_t(2));
  CHECK(removeComponentFrom(f.d, f.a, 1));  // InspNeeds itself can go
  CHECK(removeComponentFrom(f.d, f.a, 0));  // and now InspProbe
  // a fresh object gets the required type added first
  std::string c = f.d.createObject("C", "");
  CHECK(addComponentTo(f.d, c, "InspNeeds"));
  CHECK_EQ(f.d.find(c)->components.size(), size_t(2));
  CHECK(f.d.find(c)->components[0].type == "InspProbe");
  // target rules: the required type is not allowed -> refused with a reason, nothing added
  std::string e = f.d.createObject("E", "");
  OpResult blocked = addComponentTo(f.d, e, "InspNeeds", {"InspNeeds"}, "Dummy");
  CHECK(!blocked && blocked.reason.find("InspProbe") != std::string::npos);
  CHECK(f.d.find(e)->components.empty());
  CHECK(!greyReason("InspProbe", {"InspNeeds"}, "Dummy").empty());
  CHECK(greyReason("InspProbe", {}, "Dummy").empty());
}

TEST(insp_missing_script_keeps_its_json) {
  Fixture f;
  json raw = {{"speed", 3}, {"nested", json::array({1, 2})}};
  CHECK(f.d.addComponent(f.a, "GoneScript", raw));
  int idx = int(f.d.find(f.a)->components.size()) - 1;
  CHECK(isMissingScript("GoneScript"));
  CHECK(!resetComponent(f.d, f.a, idx));
  CHECK(moveComponentBy(f.d, f.a, idx, -1));
  CHECK(f.d.find(f.a)->components[idx - 1].fields == raw);
  // editing another component never touches it, and OnValidate is skipped for it
  CHECK(editFields(f.d, {cref(f.a, idx)}, "level", 50));
  CHECK(f.d.find(f.a)->components[idx - 1].fields == raw);
  // copy of a missing script still pastes into the same missing type
  CHECK(copyComponentValues(f.d, f.a, idx - 1));
  CHECK(pasteComponentValues(f.d, f.a, idx - 1));
}

TEST(insp_clipboard_survives_document_changes) {
  Fixture f;
  editFields(f.d, {cref(f.a)}, "count", 42);
  CHECK(copyComponentValues(f.d, f.a, 0));
  std::string err;
  f.d.load(templateScene(), err);  // a different document (play mode stop reloads the scene)
  CHECK(componentClipboard().has && componentClipboard().type == "InspProbe");
  CHECK_EQ(componentClipboard().fields["count"].get<int>(), 42);
}

TEST(insp_fuzzy_search_and_categories) {
  CHECK(fuzzyScore("", "Anything") == 0);
  CHECK(fuzzyScore("bxc", "BoxCollider") > 0);
  CHECK(fuzzyScore("zzz", "BoxCollider") < 0);
  CHECK(fuzzyScore("box", "BoxCollider") > fuzzyScore("box", "SoftBox"));
  CHECK(fuzzyScore("cc", "CharacterController") > fuzzyScore("cc", "Microscope"));
  CHECK(fuzzyScore("BOX COLL", "BoxCollider") > 0);
  CHECK(componentCategory(*TypeRegistry::instance().find("InspProbe")) == "Scripts");
  resetRecentComponents();
  noteComponentUsed("A");
  noteComponentUsed("B");
  noteComponentUsed("A");
  CHECK(recentComponents() == (std::vector<std::string>{"A", "B"}));
  for (int i = 0; i < 12; ++i) noteComponentUsed("T" + std::to_string(i));
  CHECK_EQ(recentComponents().size(), size_t(8));
  CHECK(recentComponents()[0] == "T11");
  resetRecentComponents();
}

TEST(insp_callbacks_leave_unchanged_fields_alone) {
  const TypeInfo* t = TypeRegistry::instance().find("InspProbe");
  json fields = {{"level", 3.0}};  // partial stored data
  bool ran = false;
  json out = runEditorCallback(*t, EditorCallback::Validate, fields, &ran);
  CHECK(ran);
  CHECK(out == fields);  // nothing to correct: nothing written
  const TypeInfo* cam = TypeRegistry::instance().find("InspNeeds");
  runEditorCallback(*cam, EditorCallback::Validate, json::object(), &ran);
  CHECK(ran);
  // a context method on a missing name is a no-op
  CHECK(runContextMethod(*t, "NoSuch", fields) == fields);
}
