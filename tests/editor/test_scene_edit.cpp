// F3.01 (Editor 2): multi-select edits on the scene document (reparent, reorder, duplicate, delete, copy/paste: one
// undo step each), the 2D layer and built-in gizmos, the 2D camera, and gate F3.G1: a scene saved, reloaded and saved
// again produces no diff.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "rensga/core/gizmos.h"
#include "rensga/editor/document_gizmos.h"
#include "rensga/editor/scene2d.h"
#include "rensga/editor/scene_document.h"
#include "rensga/editor/viewport.h"
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

// A document with a, b, c at the root, in that order, and a child a1 under a.
struct Fixture {
  SceneDocument d;
  std::string a, b, c, a1;
  Fixture() {
    fs::path dir = tmpDir("scene_edit");
    fs::copy_file(fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene", dir / "t.scene");
    std::string err;
    d.load(dir / "t.scene", err);
    a = d.createObject("A", "");
    b = d.createObject("B", "");
    c = d.createObject("C", "");
    a1 = d.createObject("A1", a);
  }
  std::vector<std::string> rootsOfInterest() const {
    std::vector<std::string> out;
    for (const std::string& g : d.childrenOf("")) if (g == a || g == b || g == c) out.push_back(g);
    return out;
  }
};

TEST(scene_edit_multiselect_basics) {
  Fixture f;
  f.d.select(f.a);
  f.d.toggleSelect(f.b);
  CHECK(f.d.isSelected(f.a) && f.d.isSelected(f.b) && !f.d.isSelected(f.c));
  CHECK_EQ(f.d.selection().size(), size_t(2));
  f.d.setSelection({f.a, f.b, f.c});
  CHECK_EQ(f.d.selected(), f.c);  // the last one is the primary
  f.d.addToSelection(f.a);
  CHECK_EQ(f.d.selected(), f.a);
  CHECK_EQ(f.d.selection().size(), size_t(3));
  // a selected child is not "top level" when its parent is selected too
  std::vector<std::string> top = f.d.topLevel({f.a, f.a1, f.b});
  CHECK_EQ(top.size(), size_t(2));
  CHECK(std::find(top.begin(), top.end(), f.a1) == top.end());
  // shift-click range in hierarchy order: A, A1, B
  std::vector<std::string> range = f.d.displayRange(f.a, f.b);
  CHECK_EQ(range.size(), size_t(3));
  CHECK_EQ(range[1], f.a1);
  // deleting a selected object drops it from the selection
  f.d.setSelection({f.a, f.b});
  CHECK(f.d.remove(f.b));
  CHECK(!f.d.isSelected(f.b));
}

TEST(scene_edit_reparent_many_keeps_world_and_is_one_undo_step) {
  Fixture f;
  const float pa[3] = {5, 0, 0}, pb[3] = {0, 7, 0}, pc[3] = {10, 0, 0}, id[4] = {0, 0, 0, 1}, one[3] = {1, 1, 1}, s2[3] = {2, 2, 2};
  CHECK(f.d.setTransform(f.a, pa, id, one));
  CHECK(f.d.setTransform(f.b, pb, id, one));
  CHECK(f.d.setTransform(f.c, pc, id, s2));
  double wa[16], wb[16];
  f.d.worldMatrix(f.a, wa);
  f.d.worldMatrix(f.b, wb);
  const size_t depth = f.d.undoDepth();
  CHECK(f.d.reparentMany({f.a, f.b}, f.c));
  CHECK_EQ(f.d.undoDepth(), depth + 1);
  CHECK_EQ(f.d.find(f.a)->parentGuid, f.c);
  CHECK_EQ(f.d.find(f.b)->parentGuid, f.c);
  double na[16], nb[16];
  f.d.worldMatrix(f.a, na);
  f.d.worldMatrix(f.b, nb);
  for (int i = 0; i < 16; ++i) { CHECK_NEAR(na[i], wa[i], 1e-3); CHECK_NEAR(nb[i], wb[i], 1e-3); }
  // the child of A went along
  CHECK_EQ(f.d.find(f.a1)->parentGuid, f.a);
  CHECK(f.d.undo());
  CHECK_EQ(f.d.undoDepth(), depth);
  CHECK(f.d.find(f.a)->parentGuid.empty());
  CHECK_NEAR(f.d.find(f.a)->position[0], 5.0, 1e-6);
  // cycles and a drop into the object's own subtree are refused and change nothing
  CHECK(!f.d.reparentMany({f.a}, f.a1));
  CHECK(!f.d.reparentMany({f.a}, f.a));
  CHECK_EQ(f.d.undoDepth(), depth);
}

TEST(scene_edit_reorder_siblings_is_file_order) {
  Fixture f;
  auto order = [&]() { return f.rootsOfInterest(); };
  CHECK(order() == (std::vector<std::string>{f.a, f.b, f.c}));
  const size_t depth = f.d.undoDepth();
  CHECK(f.d.reparentMany({f.c}, "", f.a));  // C before A
  CHECK(order() == (std::vector<std::string>{f.c, f.a, f.b}));
  CHECK_EQ(f.d.undoDepth(), depth + 1);
  CHECK(f.d.reparentMany({f.c, f.a}, "", ""));  // both to the end, keeping their relative order
  CHECK(order() == (std::vector<std::string>{f.b, f.c, f.a}));
  CHECK(!f.d.reparentMany({f.b}, "", f.b));      // before itself
  CHECK(f.d.reparentMany({f.b}, "", ""));        // already last? no: B moves behind A
  CHECK(order() == (std::vector<std::string>{f.c, f.a, f.b}));
  // moving to where it already is changes nothing and records no step
  const size_t d2 = f.d.undoDepth();
  CHECK(f.d.reparentMany({f.b}, "", ""));
  CHECK_EQ(f.d.undoDepth(), d2);
  // sibling reorder inside a parent
  const std::string a2 = f.d.createObject("A2", f.a);
  CHECK(f.d.reparentMany({a2}, f.a, f.a1));
  CHECK(f.d.childrenOf(f.a) == (std::vector<std::string>{a2, f.a1}));
  // `before` must be a sibling under the new parent
  CHECK(!f.d.reparentMany({f.c}, f.a, f.b));
  CHECK(f.d.undo());
  CHECK(f.d.undo());
}

TEST(scene_edit_duplicate_and_delete_many) {
  Fixture f;
  const size_t base = f.d.scene().objects.size();
  size_t depth = f.d.undoDepth();
  std::vector<std::string> dup = f.d.duplicateMany({f.a, f.b, f.a1});  // a1 rides along with a
  CHECK_EQ(dup.size(), size_t(2));
  CHECK_EQ(f.d.undoDepth(), depth + 1);
  CHECK_EQ(f.d.scene().objects.size(), base + 3);  // A, A1, B
  CHECK_EQ(f.d.selection().size(), size_t(2));
  // the copy of A sits right after A among the roots, the copy of B right after B
  std::vector<std::string> roots = f.d.childrenOf("");
  auto at = [&](const std::string& g) { return std::find(roots.begin(), roots.end(), g) - roots.begin(); };
  CHECK_EQ(at(dup[0]), at(f.a) + 1);
  CHECK_EQ(at(dup[1]), at(f.b) + 1);
  CHECK_EQ(f.d.find(dup[0])->name, std::string("A (1)"));
  CHECK_EQ(f.d.childrenOf(dup[0]).size(), size_t(1));
  CHECK(f.d.undo());
  CHECK_EQ(f.d.scene().objects.size(), base);

  depth = f.d.undoDepth();
  CHECK(f.d.removeMany({f.a, f.a1, f.c}));  // a1 is part of a's subtree
  CHECK_EQ(f.d.undoDepth(), depth + 1);
  CHECK_EQ(f.d.scene().objects.size(), base - 3);
  CHECK(f.d.find(f.a) == nullptr && f.d.find(f.a1) == nullptr && f.d.find(f.c) == nullptr && f.d.find(f.b) != nullptr);
  CHECK(f.d.undo());
  CHECK_EQ(f.d.scene().objects.size(), base);
  CHECK(f.d.redo());
  CHECK_EQ(f.d.scene().objects.size(), base - 3);
}

TEST(scene_edit_copy_paste_remaps_references) {
  Fixture f;
  nlohmann::json fields = {{"ref", f.a1}, {"other", f.b}};
  CHECK(f.d.addComponent(f.a, "Thing", fields));  // points at its own child and at B
  const std::string text = f.d.copyObjects({f.a});
  const size_t base = f.d.scene().objects.size();
  const size_t depth = f.d.undoDepth();
  std::vector<std::string> pasted = f.d.pasteObjects(text, "");
  CHECK_EQ(pasted.size(), size_t(1));
  CHECK_EQ(f.d.undoDepth(), depth + 1);
  CHECK_EQ(f.d.scene().objects.size(), base + 2);  // A and A1 again
  const SceneObject* np = f.d.find(pasted[0]);
  CHECK(np != nullptr);
  const std::vector<std::string> kids = f.d.childrenOf(pasted[0]);
  CHECK_EQ(kids.size(), size_t(1));
  CHECK(kids[0] != f.a1);
  CHECK_EQ(np->components[0].fields["ref"].get<std::string>(), kids[0]);  // remapped to the copy
  CHECK_EQ(np->components[0].fields["other"].get<std::string>(), f.b);    // outside the copy: unchanged
  CHECK(f.d.pasteObjects("not a clipboard", "").empty());
  CHECK(f.d.pasteObjects("{\"x\":1}", "").empty());
  CHECK(f.d.undo());
  CHECK_EQ(f.d.scene().objects.size(), base);
}

// ---- gate F3.G1 --------------------------------------------------------------------------------------

// load -> save -> load -> save must give the same bytes twice. Returns the first save's text.
static std::string saveTwice(const fs::path& src, const fs::path& dir, bool& stable) {
  fs::create_directories(dir);
  fs::copy_file(src, dir / "s.scene", fs::copy_options::overwrite_existing);
  SceneDocument d;
  std::string err;
  stable = d.load(dir / "s.scene", err) && d.save(err);
  const std::string first = slurp(dir / "s.scene");
  SceneDocument e;
  stable = stable && e.load(dir / "s.scene", err) && e.save(err);
  const std::string second = slurp(dir / "s.scene");
  stable = stable && first == second && d.document() == e.document();
  return first;
}

TEST(scene_g1_every_shipped_scene_saves_without_diff) {
  int n = 0;
  for (const char* root : {"templates", "samples"}) {
    for (auto& e : fs::recursive_directory_iterator(fs::path(RENSGA_SOURCE_DIR) / root)) {
      const std::string p = e.path().generic_string();
      if (e.path().extension() != ".scene" || p.find("/Library/") != std::string::npos || p.find("/build/") != std::string::npos) continue;
      bool stable = false;
      const std::string first = saveTwice(e.path(), tmpDir("g1_ship") / std::to_string(n), stable);
      if (!stable) std::printf("  unstable: %s\n", p.c_str());
      CHECK(stable);
      if (first != slurp(e.path())) std::printf("  note: %s is not in the editor's own format (saved text differs from the file)\n", p.c_str());
      CHECK(first == slurp(e.path()));  // shipped scenes are saved by the editor: opening and saving changes nothing
      ++n;
    }
  }
  CHECK(n >= 5);
}

TEST(scene_g1_nested_scene_with_every_field_type_saves_without_diff) {
  fs::path dir = tmpDir("g1_nested");
  fs::copy_file(fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene", dir / "n.scene");
  SceneDocument d;
  std::string err;
  CHECK(d.load(dir / "n.scene", err));
  using nlohmann::json;
  const std::string root = d.createObject("Root", "");
  const std::string mid = d.createObject("Mid", root);
  const std::string leaf = d.createObject("Leaf", mid);  // three levels
  const std::string off = d.createObject("Inactive", leaf);
  json fields = json::object({
      {"anInt", 7}, {"aNegative", -3}, {"aFloat", 2.5}, {"aTenth", 0.1}, {"aSmall", 1.5e-7}, {"aBool", true}, {"aString", "héllo \"quoted\" \\ \n tab\t"},
      {"aVec3", json::array({1.5, -2, 0.25})}, {"aColor", json::array({255, 128, 0, 255})}, {"aNull", nullptr},
      {"aGuid", leaf}, {"anArray", json::array({1, 2, 3})}, {"aNested", json::object({{"x", 1}, {"y", json::array({true, false})}})},
      {"OnOpened", json::array({json::object({{"target", leaf}, {"type", "Thing"}, {"method", "Ring"}}),
                                json::object({{"target", mid}, {"type", "Thing"}, {"method", "SetVolume"}, {"arg", 0.75}}),
                                json::object({{"target", root}, {"type", "Thing"}, {"method", "SetName"}, {"arg", "x"}}),
                                json::object({{"target", off}, {"type", "Thing"}, {"method", "Flag"}, {"arg", true}})})}});
  CHECK(d.addComponent(leaf, "Thing", fields));
  CHECK(d.setTag(mid, "Enemy"));
  CHECK(d.setLayer(mid, 5));
  CHECK(d.setActive(off, false));
  const float p[3] = {0.1f, -20.5f, 3e-3f}, q[4] = {0, 0.38268343f, 0, 0.92387953f}, s[3] = {1, 2.5f, 0.333333f};
  CHECK(d.setTransform(mid, p, q, s));
  CHECK(d.reparent(off, mid));
  CHECK(!d.duplicateMany({leaf}).empty());
  CHECK(d.undo());
  CHECK(d.redo());
  CHECK(d.save(err));
  const std::string a = slurp(dir / "n.scene");
  SceneDocument e;
  CHECK(e.load(dir / "n.scene", err));
  CHECK(e.document() == d.document());
  CHECK(e.save(err));
  CHECK(slurp(dir / "n.scene") == a);
  // and once more through a pure no-op edit path (undo stack, selection, setField with the same value)
  CHECK(e.setField(leaf, 0, "anInt", 7));
  CHECK(e.setTransform(mid, p, q, s));
  CHECK(e.save(err));
  CHECK(slurp(dir / "n.scene") == a);
  // the doc is still valid scene data
  CHECK_EQ(e.scene().objects.size(), d.scene().objects.size());
  CHECK_EQ(e.find(mid)->tag, std::string("Enemy"));
  CHECK_EQ(e.find(mid)->layer, 5);
  CHECK(!e.find(off)->active);
}

// ---- 2D layer, gizmos, 2D camera -------------------------------------------------------------------

TEST(scene_2d_gather_and_mode_detection) {
  SceneDocument room, level;
  std::string err;
  CHECK(room.load(fs::path(RENSGA_SAMPLE_DIR) / "Assets/Scenes/Room.scene", err));
  CHECK(level.load(fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene", err));
  CHECK(sceneIs2D(room));
  CHECK(!sceneIs2D(level));
  Scene2D s = gatherScene2D(room);
  CHECK_EQ(s.layers.size(), size_t(1));
  CHECK(s.sprites.size() >= 10);
  CHECK(!s.layers[0].map.empty());
  CHECK(gatherScene2D(level).sprites.empty());
  // a sprite's rectangle: size from the source rect, pivot applied
  Sprite2D sp;
  sp.model = m4Identity();
  sp.model[12] = 100;
  sp.model[13] = 50;
  sp.srcW = 32;
  sp.srcH = 16;
  sp.pivotX = 4;
  float c[12];
  CHECK(spriteWorldCorners(sp, nullptr, c));
  CHECK_NEAR(c[0], 96.0f, 1e-4);   // top-left x
  CHECK_NEAR(c[1], 50.0f, 1e-4);
  CHECK_NEAR(c[6], 128.0f, 1e-4);  // bottom-right x
  CHECK_NEAR(c[7], 66.0f, 1e-4);
  sp.srcW = 0;  // whole texture: needs the size
  CHECK(!spriteWorldCorners(sp, nullptr, c));
  sp.texture = "tex";
  CHECK(spriteWorldCorners(sp, [](const std::string&, int& w, int& h) { w = 8; h = 8; return true; }, c));
}

TEST(scene_2d_camera_zoom_keeps_the_point_under_the_cursor) {
  Editor2DCamera cam;
  cam.center[0] = 300;
  cam.center[1] = 200;
  cam.ppu = 0.8f;
  float before[2], after[2];
  cam.screenToWorld(120, 340, 900, 500, before);
  cam.zoomAt(3, 120, 340, 900, 500);
  cam.screenToWorld(120, 340, 900, 500, after);
  CHECK(cam.ppu > 0.8f);
  CHECK_NEAR(before[0], after[0], 1e-3);
  CHECK_NEAR(before[1], after[1], 1e-3);
  const float c0[2] = {cam.center[0], cam.center[1]};
  cam.pan(50, -20);  // content follows the mouse: the view centre moves the other way
  CHECK(cam.center[0] < c0[0]);
  CHECK(cam.center[1] > c0[1]);
  cam.frame(0, 0, 1280, 448, 900, 500);
  CHECK_NEAR(cam.center[0], 640.0f, 1e-3);
  CHECK(cam.ppu < 0.8f);
  // the grid step grows as the view zooms out and is always 1, 2 or 5 times a power of ten
  float last = 0;
  for (float ppu : {16.0f, 4.0f, 1.0f, 0.25f, 0.05f}) {
    cam.ppu = ppu;
    const float s = cam.gridStep();
    CHECK(s >= last);
    CHECK(s * ppu >= 11.9f);
    last = s;
  }
}

TEST(scene_gizmos_from_the_document) {
  SceneDocument room;
  std::string err;
  CHECK(room.load(fs::path(RENSGA_SAMPLE_DIR) / "Assets/Scenes/Room.scene", err));
  DocumentGizmoContext ctx;
  ctx.is2D = true;
  ctx.tilemapSize = [](const std::string&, int& w, int& h, int& ts) { w = 40; h = 14; ts = 32; return true; };
  GizmoDraw::clear();
  buildDocumentGizmos(room, ctx);
  const unsigned unselected = GizmoDraw::lineCount();
  CHECK(unselected > 0);
  // every BoxCollider2D is a rectangle (4 lines), the tilemap bounds another, Camera2D three (view, dead zone, bounds)
  unsigned colliders = 0;
  for (const SceneObject& o : room.scene().objects)
    for (const SceneComponent& c : o.components) if (c.type == "BoxCollider2D") ++colliders;
  CHECK_EQ(unselected, colliders * 4 + 4 + 12);
  // selecting the tilemap adds its tile grid
  for (const SceneObject& o : room.scene().objects) if (o.name == "Tilemap") room.select(o.guid);
  GizmoDraw::clear();
  buildDocumentGizmos(room, ctx);
  CHECK(GizmoDraw::lineCount() > unselected + 39 + 13 - 1);
  GizmoDraw::clear();
}

// ---- ID-buffer picking helpers (CPU part of the picking rays still used for the context menu) ----------

TEST(scene_view_matrices_2d_flip_y) {
  Editor2DCamera cam;
  cam.ppu = 2;
  const M4 vp = m4Mul(cam.proj(800, 600), cam.view());
  const float p[3] = {10, 10, 0};  // below and right of the origin in pixel space: screen y goes down
  float clip[3];
  m4Point(vp, p, clip);
  CHECK(clip[0] > 0);
  CHECK(clip[1] < 0);
  CHECK_NEAR(clip[0], 10.0f * 2 * 2 / 800, 1e-5);
}
