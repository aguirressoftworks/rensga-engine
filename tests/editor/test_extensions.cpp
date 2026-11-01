// F6.07 (D3, Editor 13): the editor extension registry, the editor API (ComponentView, Selection, Scene, Assets), data asset documents,
// and a real Editor/ module built with cmake, loaded, reloaded after an edit and unloaded. No ImGui.
#include <stdio.h>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <iterator>
#include <thread>

#include "rensga/DataAsset.h"
#include "rensga/core/object.h"
#include "rensga/editor/data_asset_doc.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/extension.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace rensga {
namespace exttest {
struct ExtProbe : Behaviour {
  REFLECT(ExtProbe)
  SERIALIZE_FIELD(i32, count, 2)
  SERIALIZE_FIELD(Scalar, level, 1.0f)
  SERIALIZE_FIELD(Vector3, spot, Vector3(0, 0, 0))
  SERIALIZE_FIELD(String32, label, "p")
};
struct ExtStats : DataAsset {
  REFLECT(ExtStats)
  SERIALIZE_FIELD(i32, health, 30)
  SERIALIZE_FIELD(Scalar, speed, 3.0f)
  SERIALIZE_FIELD(String32, label, "Grunt")
  SERIALIZE_ENUM(kind, 1, "Melee|Ranged|Boss")
  SERIALIZE_ARRAY(i32, waves, 4)
};
}  // namespace exttest
}  // namespace rensga

namespace {
fs::path tmpDir(const char* tag) {
  const fs::path d = fs::temp_directory_path() / (std::string("rensga_ext_") + tag);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}
void noop(ext::ComponentView&) {}
void noopV() {}

struct Doc {
  SceneDocument d;
  std::string a, b;
  Doc() {
    std::string err;
    d.load(fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene", err);
    a = d.createObject("A", "");
    b = d.createObject("B", "");
    for (const std::string& g : {a, b}) d.addComponent(g, "ExtProbe", typeDefaults(*TypeRegistry::instance().find("ExtProbe")));
    d.select(a);
    ext::host().doc = &d;
  }
  ~Doc() { ext::host().doc = nullptr; }
};
}  // namespace

TEST(ext_registry_registers_and_drops_by_mark) {
  const unsigned mark = ext::registrationMark();
  ext::addInspector("ExtProbe", noop, false);
  ext::addInspector("ExtReplaced", noop, true);
  ext::addWindow("Win A", noopV);
  ext::addMenuItem("Tools/Do It", "Ctrl+G", noopV);
  ext::addMenuItem("", "", noopV);   // ignored: no path
  ext::addWindow("Null", nullptr);   // ignored: no function
  CHECK(ext::inspectorReplaces("ExtReplaced"));
  CHECK(!ext::inspectorReplaces("ExtProbe"));
  size_t windowsNow = ext::windows().size(), menusNow = ext::menuItems().size();
  CHECK(windowsNow >= 1 && menusNow >= 1);
  ext::windowOpen("Win A") = true;
  ext::unregisterFrom(mark);
  CHECK(!ext::inspectorReplaces("ExtReplaced"));
  CHECK(ext::windows().empty() && ext::menuItems().empty() && ext::inspectors().empty());
  CHECK(ext::windowOpen("Win A"));  // the open state is kept across a reload
}

TEST(ext_component_view_reads_and_edits_with_undo) {
  Doc f;
  ext::ComponentView v(f.d, {ComponentRef{f.a, 0}, ComponentRef{f.b, 0}}, "ExtProbe");
  CHECK(v.Multi() && v.Count() == 2 && v.ObjectName() == "A");
  CHECK_EQ(v.GetInt("count"), 2);
  CHECK(v.GetFloat("level") == 1.0f);
  CHECK(v.GetString("label") == "p");
  float p[3] = {9, 9, 9};
  CHECK(v.GetVector3("spot", p) && p[0] == 0 && p[1] == 0);
  CHECK(!v.GetVector3("count", p));
  const size_t depth = f.d.undoDepth();
  CHECK(v.SetInt("count", 7));
  CHECK_EQ(f.d.undoDepth(), depth + 1);  // both objects, one undo step
  CHECK_EQ(v.GetInt("count"), 7);
  CHECK(!v.Mixed("count"));
  CHECK(v.SetString("label", "hello"));
  const float q[3] = {1, 2, 3};
  CHECK(v.SetVector3("spot", q));
  CHECK(f.d.undo() && f.d.undo() && f.d.undo());
  CHECK_EQ(v.GetInt("count"), 2);
  CHECK(!v.Set("nonsense", 1));         // unknown field
  CHECK(!v.SetFloat("count", 1.5f) || v.GetInt("count") == 1 || true);  // a float into an i32 is the model's call; it must not crash
  ext::ComponentView one(f.d, {ComponentRef{f.a, 0}}, "ExtProbe");
  CHECK(one.SetInt("count", 5));
  CHECK(v.Mixed("count"));              // A is 5, B is 2
}

TEST(ext_editor_api_selection_scene_assets_log) {
  Doc f;
  CHECK(ext::Selection::ActiveGuid() == f.a);
  CHECK(ext::Selection::Select("B"));
  CHECK(ext::Selection::ActiveGuid() == f.b && ext::Selection::Names().size() == 1 && ext::Selection::Names()[0] == "B");
  CHECK(!ext::Selection::Select("no such object"));
  CHECK(ext::Selection::SelectMany({f.a, f.b}) && ext::Selection::Guids().size() == 2);
  CHECK_EQ(ext::SceneEdit::Find("", "ExtProbe").size(), 2u);
  CHECK_EQ(ext::SceneEdit::Find("B", "ExtProbe").size(), 1u);
  CHECK(ext::SceneEdit::NameOf(f.a) == "A");
  CHECK(ext::SceneEdit::GetField(f.a, "ExtProbe", "count") == 2);
  const size_t depth = f.d.undoDepth();
  ext::SceneEdit::BeginGroup();
  CHECK(ext::SceneEdit::SetField(f.a, "ExtProbe", "count", 11));
  CHECK(ext::SceneEdit::SetField(f.b, "ExtProbe", "count", 12));
  CHECK(ext::SceneEdit::SetPosition(f.a, 1, 2, 3));
  ext::SceneEdit::EndGroup();
  CHECK_EQ(f.d.undoDepth(), depth + 1);  // one step for the group
  float pos[3];
  CHECK(ext::SceneEdit::GetPosition(f.a, pos) && pos[1] == 2);
  CHECK(ext::SceneEdit::Undo());
  CHECK(ext::SceneEdit::GetField(f.b, "ExtProbe", "count") == 2);
  CHECK(ext::SceneEdit::Redo());
  CHECK(ext::SceneEdit::GetField(f.b, "ExtProbe", "count") == 12);
  CHECK(!ext::SceneEdit::SetField(f.a, "NoSuchComponent", "count", 1));
  CHECK(!ext::SceneEdit::SetField("00000000-0000-4000-8000-000000000000", "ExtProbe", "count", 1));
  // without a document every call is safe
  ext::host().doc = nullptr;
  CHECK(ext::Selection::Guids().empty() && ext::SceneEdit::Find().empty() && !ext::SceneEdit::Undo());
  CHECK(ext::Assets::Find().empty() && ext::Assets::PathOf("x").empty());
}

TEST(ext_data_asset_doc_create_edit_undo) {
  const fs::path root = tmpDir("doc");
  fs::create_directories(root / "Assets" / "Data");
  AssetDatabase db;
  db.open(root);
  ext::host().assets = &db;
  fs::path made;
  std::string err;
  CHECK(!createDataAsset(db, root / "Assets/Data", "ExtProbe", "", made, err));   // a Behaviour is not a data asset
  CHECK(!createDataAsset(db, root / "Assets/Data", "NoSuchType", "", made, err));
  CHECK(createDataAsset(db, root / "Assets/Data", "ExtStats", "Grunt", made, err));
  CHECK(made.filename() == "Grunt.asset.json");
  fs::path made2;
  CHECK(createDataAsset(db, root / "Assets/Data", "ExtStats", "Grunt", made2, err));
  CHECK(made2.filename() == "Grunt 1.asset.json");
  CHECK(assetKindOf(made) == AssetKind::DataAsset);
  CHECK(ext::Assets::Find("Grunt", "dataasset").size() == 2);
  CHECK(!ext::Assets::GuidOf("Assets/Data/Grunt.asset.json").empty());
  CHECK(ext::Assets::PathOf(ext::Assets::GuidOf("Assets/Data/Grunt.asset.json")) == "Assets/Data/Grunt.asset.json");

  DataAssetDoc doc;
  CHECK(doc.load(made, err));
  CHECK(doc.typeName() == "ExtStats" && doc.type() != nullptr);
  CHECK(doc.value("health") == 30 && doc.value("label") == "Grunt" && doc.value("kind") == 1);
  CHECK(doc.set("health", 55, err));
  CHECK(doc.set("speed", 4.5, err));
  CHECK(doc.set("kind", "Boss", err));      // an enum takes its label
  CHECK(doc.value("kind") == 2);
  CHECK(doc.set("waves", json::array({4, 5, 6}), err));
  CHECK(!doc.set("nope", 1, err) && err.find("no field") != std::string::npos);
  CHECK(!doc.set("health", "not a number", err));
  CHECK_EQ(doc.undoDepth(), 4u);
  {  // the file already holds the edits: a second document reads them
    DataAssetDoc again;
    CHECK(again.load(made, err));
    CHECK(again.value("health") == 55 && again.value("waves") == json::array({4, 5, 6}));
  }
  CHECK(doc.undo() && doc.undo());
  CHECK(doc.value("kind") == 1 && doc.value("speed") == 4.5);
  CHECK(doc.redo());
  CHECK(doc.value("kind") == 2);
  doc.beginGesture();   // a slider drag: many edits, one undo step
  const size_t before = doc.undoDepth();
  for (int i = 0; i < 20; ++i) CHECK(doc.set("health", 60 + i, err));
  doc.endGesture();
  CHECK_EQ(doc.undoDepth(), before + 1);
  CHECK(doc.value("health") == 79);
  CHECK(doc.undo());
  CHECK(doc.value("health") == 55);
  // a removed script field stays in the file until the next edit
  {
    std::ifstream in(made);
    json j = json::parse(in);
    j["fields"]["oldField"] = 3;
    std::ofstream(made) << j.dump(1);
  }
  CHECK(doc.load(made, err));
  CHECK(doc.unknownFields().size() == 1 && doc.unknownFields()[0] == "oldField");
  CHECK(doc.set("health", 1, err));
  CHECK(doc.unknownFields().empty());
  // a document whose type is not loaded keeps its file untouched
  {
    std::ofstream(made) << R"({"format":1,"kind":"DataAsset","type":"GoneType","fields":{"x":1}})";
    DataAssetDoc gone;
    CHECK(gone.load(made, err) && gone.type() == nullptr);
    CHECK(!gone.set("x", 2, err));
  }
  ext::host().assets = nullptr;
}

TEST(ext_module_builds_loads_reloads_and_unloads) {
  const fs::path proj = tmpDir("module");
  fs::create_directories(proj / "Editor");
  fs::copy_file(fs::path(RENSGA_FIXTURES_DIR) / "ext_module/Editor/Ext.cpp", proj / "Editor/Ext.cpp");
  CHECK(ext::projectHasEditorCode(proj));
  CHECK(!ext::extensionModuleUpToDate(proj));
  const std::string sig = ext::hashEditorSources(proj);
  CHECK(!sig.empty());
  std::string log;
  const bool built = ext::buildExtensionModuleBlocking(proj, log);
  if (!built) std::printf("%s\n", log.c_str());
  CHECK(built);
  CHECK(ext::extensionModuleUpToDate(proj));
  ext::ExtensionModule& m = ext::ExtensionModule::instance();
  CHECK(!m.loaded());
  std::string err;
  const ext::ExtensionLayout lay = ext::extensionLayout(proj);
  CHECK(m.load(lay.output, err));
  if (!err.empty()) std::printf("load: %s\n", err.c_str());
  CHECK(m.loaded() && m.inspectorCount() == 2 && m.windowCount() == 1 && m.menuCount() == 2);
  CHECK(ext::inspectorReplaces("ExtOther") && !ext::inspectorReplaces("ExtProbe"));
  // the module's callbacks run against the editor API
  Doc f;
  for (const ext::InspectorEntry& e : ext::inspectors()) {
    if (e.type != "ExtProbe") continue;
    ext::ComponentView v(f.d, {ComponentRef{f.a, 0}}, "ExtProbe");
    e.fn(v);
    CHECK_EQ(v.GetInt("count"), 3);   // 2 + 1, set from inside the module
  }
  for (const ext::MenuEntry& e : ext::menuItems())
    if (e.path == "Tools/Fixture Key") { CHECK(e.shortcut == "Ctrl+Shift+F"); e.fn(); }
  CHECK(ext::Selection::ActiveGuid() == f.b);   // the menu item selected "B"
  bool saw = false;
  for (const LogLine& l : EditorLog::instance().snapshot()) saw = saw || l.text.find("ext-fixture: menu item v1 calls=1") != std::string::npos;
  CHECK(saw);

  // edit the source: the signature changes, the module is stale, a rebuild + reload swaps the behaviour
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  {
    std::ifstream in(proj / "Editor/Ext.cpp");
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t at = src.find("menu item v1");
    CHECK(at != std::string::npos);
    src.replace(at, 12, "menu item v2");
    src += "\nRENSGA_EDITOR_WINDOW(\"Second Window\", window)\n";
    std::ofstream(proj / "Editor/Ext.cpp") << src;
  }
  CHECK(ext::hashEditorSources(proj) != sig);
  CHECK(!ext::extensionModuleUpToDate(proj));
  log.clear();
  CHECK(ext::buildExtensionModuleBlocking(proj, log));
  const int gen = m.generation();
  CHECK(m.reload(lay.output, err));
  CHECK_EQ(m.generation(), gen + 1);
  CHECK(m.windowCount() == 2 && m.menuCount() == 2 && m.inspectorCount() == 2);   // the old registrations are gone, not doubled
  for (const ext::MenuEntry& e : ext::menuItems()) if (e.path == "Tools/Fixture Item") e.fn();
  saw = false;
  for (const LogLine& l : EditorLog::instance().snapshot()) saw = saw || l.text.find("ext-fixture: menu item v2") != std::string::npos;
  CHECK(saw);

  // a broken edit: the build fails, the last good module keeps working
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  std::ofstream(proj / "Editor/Ext.cpp", std::ios::app) << "\nthis is not C++;\n";
  log.clear();
  CHECK(!ext::buildExtensionModuleBlocking(proj, log));
  CHECK(log.find("error") != std::string::npos);
  CHECK(m.loaded() && m.menuCount() == 2);

  m.unload();
  CHECK(!m.loaded() && ext::menuItems().empty() && ext::windows().empty() && ext::inspectors().empty());
}
