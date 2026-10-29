#include <filesystem>

#include "rensga/editor/scene_document.h"
#include "rensga/editor/undo.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;

namespace {
// Sets a shared int; remembers old/new. Key merges drags.
struct SetInt : Command {
  int* target; int from, to; std::string key;
  SetInt(int* t, int f, int n, std::string k = "") : target(t), from(f), to(n), key(std::move(k)) {}
  const char* name() const override { return "Set int"; }
  void undo() override { *target = from; }
  void redo() override { *target = to; }
  const std::string& mergeKey() const override { return key; }
  bool mergeWith(const Command& next) override { to = static_cast<const SetInt&>(next).to; return true; }
};
}  // namespace

TEST(undo_basic_redo_and_branch_cut) {
  UndoStack s;
  int v = 0;
  s.execute(std::make_unique<SetInt>(&v, 0, 1));
  s.execute(std::make_unique<SetInt>(&v, 1, 2));
  CHECK_EQ(s.depth(), size_t(2));
  CHECK(s.undo()); CHECK_EQ(v, 1);
  CHECK(s.undo()); CHECK_EQ(v, 0);
  CHECK(!s.undo());
  CHECK(s.redo()); CHECK_EQ(v, 1);
  s.execute(std::make_unique<SetInt>(&v, 1, 9));  // cuts the redo branch
  CHECK(!s.canRedo());
  CHECK(s.undo()); CHECK_EQ(v, 1);
}

TEST(undo_merges_consecutive_drags) {
  UndoStack s;
  s.setMergeWindowMs(1000000);
  int v = 0;
  for (int i = 1; i <= 50; ++i) s.execute(std::make_unique<SetInt>(&v, i - 1, i, "drag"));
  CHECK_EQ(s.depth(), size_t(1));
  CHECK(s.undo()); CHECK_EQ(v, 0);
  CHECK(s.redo()); CHECK_EQ(v, 50);
  s.execute(std::make_unique<SetInt>(&v, 50, 51, "other"));  // different key: new step
  CHECK_EQ(s.depth(), size_t(2));
  s.breakMerge();
  s.execute(std::make_unique<SetInt>(&v, 51, 52, "other"));  // merge run was ended
  CHECK_EQ(s.depth(), size_t(3));
  s.setMergeWindowMs(0);
  UndoStack t;
  t.setMergeWindowMs(-1);
  int w = 0;
  t.execute(std::make_unique<SetInt>(&w, 0, 1, "k"));
  t.execute(std::make_unique<SetInt>(&w, 1, 2, "k"));
  CHECK_EQ(t.depth(), size_t(2));  // outside the window: no merge
}

TEST(undo_groups_are_one_step_and_nest) {
  UndoStack s;
  int a = 0, b = 0;
  s.beginGroup("two");
  s.execute(std::make_unique<SetInt>(&a, 0, 1));
  s.beginGroup("inner");
  s.execute(std::make_unique<SetInt>(&b, 0, 2));
  s.endGroup();
  CHECK_EQ(s.depth(), size_t(0));  // outermost not closed yet
  s.execute(std::make_unique<SetInt>(&a, 1, 3));
  s.endGroup();
  CHECK_EQ(s.depth(), size_t(1));
  CHECK(a == 3 && b == 2);
  CHECK(s.undo());
  CHECK(a == 0 && b == 0);  // reverse order restores the first value
  CHECK(s.redo());
  CHECK(a == 3 && b == 2);
  s.beginGroup(); s.endGroup();  // empty group records nothing
  CHECK_EQ(s.depth(), size_t(1));
}

TEST(undo_depth_limit_and_dirty_tracking) {
  UndoStack s;
  int v = 0;
  CHECK(!s.dirty());
  for (int i = 0; i < 400; ++i) s.execute(std::make_unique<SetInt>(&v, i, i + 1));
  CHECK_EQ(s.depth(), UndoStack::kDefaultLimit);
  CHECK(UndoStack::kDefaultLimit >= 200);
  CHECK(s.dirty());
  s.markClean();
  CHECK(!s.dirty());
  s.undo();
  CHECK(s.dirty());
  s.redo();
  CHECK(!s.dirty());  // back at the saved state
  s.undo();
  s.execute(std::make_unique<SetInt>(&v, 0, 5));  // saved state is now unreachable
  CHECK(s.dirty());
  s.undo();
  CHECK(s.dirty());
  s.redo();
  CHECK(s.dirty());
  UndoStack u(3);  // saved state falls off the end
  u.execute(std::make_unique<SetInt>(&v, 0, 1));
  u.markClean();
  for (int i = 0; i < 5; ++i) u.execute(std::make_unique<SetInt>(&v, 0, 1));
  for (int i = 0; i < 3; ++i) u.undo();
  CHECK(u.dirty());
}

TEST(undo_does_not_merge_into_saved_state) {
  UndoStack s;
  s.setMergeWindowMs(1000000);
  int v = 0;
  s.execute(std::make_unique<SetInt>(&v, 0, 1, "drag"));
  s.markClean();
  s.execute(std::make_unique<SetInt>(&v, 1, 2, "drag"));
  CHECK_EQ(s.depth(), size_t(2));
  CHECK(s.dirty());
  s.undo();
  CHECK(!s.dirty());
  CHECK_EQ(v, 1);
}

static fs::path templateScene() { return fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene"; }

TEST(undo_scene_document_drag_group_dirty_and_200_steps) {
  SceneDocument d;
  std::string err;
  CHECK(d.load(templateScene(), err));
  CHECK(!d.dirty());
  std::string g = d.scene().objects[0].guid;
  d.undoStack().setMergeWindowMs(1000000);
  float p[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, sc[3] = {1, 1, 1};
  for (int i = 1; i <= 20; ++i) {  // a drag without an explicit gesture merges by key
    p[0] = float(i);
    CHECK(d.setTransform(g, p, r, sc));
  }
  CHECK_EQ(d.undoDepth(), size_t(1));
  CHECK(d.dirty());
  CHECK(d.undo());
  CHECK(!d.dirty());  // undo reached the loaded state
  CHECK(d.redo());
  CHECK(d.dirty());
  d.undoStack().breakMerge();
  d.beginGesture();  // grouped: rename + transform = one step
  d.rename(g, "grouped");
  p[1] = 4; d.setTransform(g, p, r, sc);
  d.endGesture();
  CHECK_EQ(d.undoDepth(), size_t(2));
  for (int i = 0; i < 210; ++i) d.rename(g, "r" + std::to_string(i));
  CHECK(d.undoDepth() >= 200);
  size_t n = d.undoDepth();
  for (size_t i = 0; i < n; ++i) CHECK(d.undo());
  CHECK(!d.canUndo());
  std::string path = (fs::path(RENSGA_TEST_TMP_DIR) / "undo_scene.scene").string();
  fs::create_directories(RENSGA_TEST_TMP_DIR);
  SceneDocument e;
  CHECK(e.load(templateScene(), err));
  fs::copy_file(templateScene(), path, fs::copy_options::overwrite_existing);
  CHECK(e.restore(path, e.document(), false));
  e.rename(e.scene().objects[0].guid, "saved");
  CHECK(e.dirty());
  CHECK(e.save(err));
  CHECK(!e.dirty());
  CHECK(e.undo());
  CHECK(e.dirty());  // differs from the file on disk
}
