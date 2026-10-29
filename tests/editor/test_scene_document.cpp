#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "rensga/cooker/cooker.h"
#include "rensga/editor/scene_document.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static fs::path tmpDir(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}

static fs::path templateScene() { return fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene"; }

TEST(scene_doc_guid_is_v4) {
  std::string g = generateGuid();
  CHECK_EQ(g.size(), 36u);
  CHECK(g[8] == '-' && g[13] == '-' && g[18] == '-' && g[23] == '-');
  CHECK(g[14] == '4');
  CHECK(g[19] == '8' || g[19] == '9' || g[19] == 'a' || g[19] == 'b');
  CHECK(generateGuid() != g);
}

TEST(scene_doc_roundtrip_is_byte_identical) {
  SceneDocument d;
  std::string err;
  CHECK(d.load(templateScene(), err));
  CHECK(!d.dirty());
  fs::path out = tmpDir("roundtrip") / "TestLevel.scene";
  fs::copy_file(templateScene(), out);
  SceneDocument d2;
  CHECK(d2.load(out, err));
  CHECK(d2.save(err));
  CHECK(slurp(out) == slurp(templateScene()));
}

TEST(scene_doc_edit_undo_redo) {
  SceneDocument d;
  std::string err;
  CHECK(d.load(templateScene(), err));
  CHECK(!d.scene().objects.empty());
  std::string g = d.scene().objects[0].guid;
  float p[3] = {1, 2.5f, -3}, r[4] = {0, 0, 0, 1}, s[3] = {2, 2, 2};
  CHECK(d.setTransform(g, p, r, s));
  CHECK(d.dirty());
  CHECK(d.find(g)->position[1] == 2.5f);
  CHECK(d.undo());
  CHECK(d.find(g)->position[1] != 2.5f);
  CHECK(d.canRedo());
  CHECK(d.redo());
  CHECK(d.find(g)->position[1] == 2.5f);
  CHECK(d.rename(g, "Floor"));
  CHECK(d.find(g)->name == "Floor");
  CHECK(d.undo());
  CHECK(d.find(g)->name != "Floor");
  // gesture: many drags, one undo step
  size_t depth = d.undoDepth();
  d.beginGesture();
  for (int i = 0; i < 10; ++i) { p[0] = float(i); d.setTransform(g, p, r, s); }
  d.endGesture();
  CHECK_EQ(d.undoDepth(), depth + 1);
  // setField / add / remove component
  int n = int(d.find(g)->components.size());
  CHECK(d.addComponent(g, "Thing", nlohmann::json{{"a", 1}}));
  CHECK_EQ(int(d.find(g)->components.size()), n + 1);
  CHECK(d.setField(g, n, "a", 7));
  CHECK_EQ(d.find(g)->components[n].fields["a"].get<int>(), 7);
  CHECK(d.removeComponent(g, n));
  CHECK_EQ(int(d.find(g)->components.size()), n);
  CHECK(!d.setField(g, 99, "a", 1));
}

TEST(scene_doc_undo_limit) {
  SceneDocument d;
  std::string err;
  CHECK(d.load(templateScene(), err));
  std::string g = d.scene().objects[0].guid;
  for (int i = 0; i < 250; ++i) d.rename(g, "n" + std::to_string(i));
  CHECK_EQ(d.undoDepth(), size_t(200));
}

TEST(scene_doc_duplicate_and_delete_with_children) {
  SceneDocument d;
  std::string err;
  CHECK(d.load(templateScene(), err));
  size_t base = d.scene().objects.size();
  std::string a = d.createObject("A", "");
  std::string b = d.createObject("B", a);
  std::string c = d.createObject("C", b);
  SceneComponent comp;
  comp.type = "Thing";
  comp.fields = {{"ref", c}};  // C points at itself: a ref inside the duplicated subtree
  CHECK(d.addComponent(c, "Thing", comp.fields));
  CHECK_EQ(d.scene().objects.size(), base + 3);
  std::string a2 = d.duplicate(a);
  CHECK(!a2.empty() && a2 != a);
  CHECK_EQ(d.scene().objects.size(), base + 6);
  CHECK_EQ(d.selected(), a2);
  // children were copied with new GUIDs and the parent chain remapped
  std::vector<std::string> kids = d.childrenOf(a2);
  CHECK_EQ(kids.size(), size_t(1));
  CHECK(kids[0] != b);
  std::vector<std::string> grand = d.childrenOf(kids[0]);
  CHECK_EQ(grand.size(), size_t(1));
  CHECK(grand[0] != c);
  CHECK(d.find(grand[0])->components[0].fields["ref"].get<std::string>() == grand[0]);  // remapped
  CHECK(d.find(c)->components[0].fields["ref"].get<std::string>() == c);                 // original untouched
  CHECK(d.remove(a));
  CHECK_EQ(d.scene().objects.size(), base + 3);
  CHECK(d.find(b) == nullptr && d.find(c) == nullptr);
  CHECK(d.undo());
  CHECK_EQ(d.scene().objects.size(), base + 6);
}

TEST(scene_doc_reparent_keeps_world) {
  SceneDocument d;
  std::string err;
  CHECK(d.load(templateScene(), err));
  std::string p = d.createObject("P", "");
  std::string o = d.createObject("O", "");
  float pp[3] = {10, 0, 0}, r[4] = {0, 0.7071068f, 0, 0.7071068f}, s[3] = {2, 2, 2};
  float op[3] = {1, 2, 3}, ir[4] = {0, 0, 0, 1}, os[3] = {1, 1, 1};
  CHECK(d.setTransform(p, pp, r, s));
  CHECK(d.setTransform(o, op, ir, os));
  double before[16], after[16];
  d.worldMatrix(o, before);
  CHECK(d.reparent(o, p));
  d.worldMatrix(o, after);
  for (int i = 0; i < 16; ++i) CHECK_NEAR(before[i], after[i], 1e-3);
  CHECK(!d.reparent(p, o));  // cycle
  CHECK(d.reparent(o, ""));
  d.worldMatrix(o, after);
  for (int i = 0; i < 16; ++i) CHECK_NEAR(before[i], after[i], 1e-3);
}

TEST(scene_doc_selection_follows_object) {
  SceneDocument d;
  std::string err;
  CHECK(d.load(templateScene(), err));
  std::string a = d.createObject("A", "");
  d.select(a);
  CHECK_EQ(d.selected(), a);
  CHECK(d.undo());  // the object is gone, so is the selection
  CHECK(d.selected().empty());
}

TEST(scene_doc_saved_scene_still_cooks) {
  fs::path proj = tmpDir("cook") / "proj";
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok", proj, fs::copy_options::recursive);
  fs::path sc = proj / "Assets/Scenes/test.scene";
  SceneDocument d;
  std::string err;
  CHECK(d.load(sc, err));
  // the fixture's TestMover is a test-only script type; drop it, then edit and save
  for (const SceneObject& o : std::vector<SceneObject>(d.scene().objects))
    for (size_t i = 0; i < o.components.size(); ++i)
      if (o.components[i].type == "TestMover") d.removeComponent(o.guid, int(i));
  std::string root = d.scene().objects[0].guid;
  float p[3] = {0, 3, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  CHECK(d.setTransform(root, p, r, s));
  std::string dup = d.duplicate(root);
  CHECK(!dup.empty());
  CHECK(d.save(err));
  CHECK(!d.dirty());
  CookOptions o;
  o.project = proj;
  o.force = true;
  o.genHeader = proj / "Library" / "Generated" / "console_target.gen.h";
  CookResult res = cookProject(o, [](const CookMessage& m) { fprintf(stderr, "    [cook] %s\n", m.text.c_str()); });
  CHECK(res.ok);
}
