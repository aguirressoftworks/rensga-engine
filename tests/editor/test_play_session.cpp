// F3.04: Play in editor on the editor core, no GL. A temp copy of the cook_ok fixture holds a test script (PlayMover) that moves
// its object by `speed` in every FixedUpdate, so frame counts are exact.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "rensga/Rensga.h"
#include "rensga/editor/play_session.h"
#include "rensga/editor/scene_document.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

struct PlayMover : Behaviour {
  REFLECT(PlayMover)
  SERIALIZE_FIELD(Guid, target, Guid())
  SERIALIZE_FIELD(Scalar, speed, 1.0f)
  SERIALIZE_FIELD(i32, lives, 3)
  SERIALIZE_FIELD(bool, flying, false)
  void FixedUpdate() override {
    Vector3 p = transform()->localPosition();
    transform()->setLocalPosition(Vector3(p.x + speed, p.y, p.z));
  }
};

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream i(p, std::ios::binary);
  std::ostringstream s;
  s << i.rdbuf();
  return s.str();
}

// Copy of cook_ok whose Cam object carries a PlayMover instead of the unregistered TestMover.
fs::path makeProject(const char* name, int trianglesPerFrame = 20000) {
  const fs::path dst = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst.parent_path());
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok", dst, fs::copy_options::recursive);
  fs::remove_all(dst / "Library");
  const fs::path scene = dst / "Assets" / "Scenes" / "test.scene";
  std::string txt = slurp(scene);
  for (size_t p; (p = txt.find("\"TestMover\"")) != std::string::npos;) txt.replace(p, 11, "\"PlayMover\"");
  std::ofstream(scene, std::ios::binary | std::ios::trunc) << txt;
  const fs::path ct = dst / "ProjectSettings" / "ConsoleTarget.json";
  json j = json::parse(slurp(ct));
  j["allowedComponents"].push_back("PlayMover");
  j["budgets"]["trianglesPerFrame"] = trianglesPerFrame;
  std::ofstream(ct, std::ios::binary | std::ios::trunc) << j.dump(2);
  return dst;
}

const SceneObject* byName(const Scene& s, const char* n) {
  for (const SceneObject& o : s.objects) if (o.name == n) return &o;
  return nullptr;
}

int moverIndex(const SceneObject& o) {
  for (size_t i = 0; i < o.components.size(); ++i) if (o.components[i].type == "PlayMover") return static_cast<int>(i);
  return -1;
}

bool startOk(PlaySession& ps, SceneDocument& doc, const fs::path& proj) {
  std::string e;
  const bool ok = ps.start(doc, proj, "psp", e);
  if (!ok) fprintf(stderr, "    play start failed: %s\n", e.c_str());
  return ok;
}

bool near(float a, float b) { return a - b < 0.001f && b - a < 0.001f; }

VirtualGamepad noPad() { return VirtualGamepad(); }

}  // namespace

TEST(play_pause_step_exact_frame_counts) {
  const fs::path proj = makeProject("play_step");
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(proj / "Assets" / "Scenes" / "test.scene", err));
  PlaySession ps;
  CHECK(startOk(ps, doc, proj));
  CHECK(ps.active());
  CHECK(doc.playing());
  const SceneObject* player = byName(doc.scene(), "Cam");
  CHECK(player != nullptr);
  const float x0 = player->position[0];
  // five editor frames of exactly one fixed step each
  for (int i = 0; i < 5; ++i) ps.tick(1.0 / 60.0, noPad());
  CHECK(ps.frames() == 5);
  CHECK(ps.fixedSteps() == 5);
  const float step = (byName(doc.scene(), "Cam")->position[0] - x0) / 5.0f;  // the scene sets the script's speed
  CHECK(step > 0.5f);
  CHECK(near(byName(doc.scene(), "Cam")->position[0], x0 + 5.0f * step));

  ps.setPaused(true);
  CHECK(ps.state() == PlayState::Paused);
  const uint64_t f0 = ps.frames(), s0 = ps.fixedSteps();
  ps.tick(0.5, noPad());  // paused: time passes, the World does not
  CHECK(ps.frames() == f0 && ps.fixedSteps() == s0);
  for (int i = 0; i < 7; ++i) ps.requestStep();
  ps.tick(0.5, noPad());
  CHECK(ps.frames() == f0 + 7);
  CHECK(ps.fixedSteps() == s0 + 7);
  CHECK(near(byName(doc.scene(), "Cam")->position[0], x0 + 12.0f * step));
  ps.tick(0.5, noPad());
  CHECK(ps.fixedSteps() == s0 + 7);  // no request, no step
  ps.requestStep();
  ps.setPaused(false);                // resuming drops pending steps
  ps.tick(1.0 / 60.0, noPad());
  CHECK(ps.fixedSteps() == s0 + 8);
  ps.stop(doc);
  CHECK(!ps.active());
  CHECK(!doc.playing());
}

TEST(play_stop_restores_document_selection_and_undo) {
  const fs::path proj = makeProject("play_revert");
  const fs::path scene = proj / "Assets" / "Scenes" / "test.scene";
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(scene, err));
  const std::string fileBefore = slurp(scene);
  const SceneObject* ground = byName(doc.scene(), "Ground");
  CHECK(ground != nullptr);
  const std::string groundGuid = ground->guid;
  CHECK(doc.rename(groundGuid, "Floor"));  // one undo step and a dirty document before Play
  doc.select(groundGuid);
  const ordered_json docBefore = doc.document();
  const size_t undoBefore = doc.undoDepth();
  const bool dirtyBefore = doc.dirty();
  CHECK(undoBefore >= 1 && dirtyBefore);

  PlaySession ps;
  CHECK(startOk(ps, doc, proj));
  ps.tick(1.0 / 60.0, noPad());
  const SceneObject* player = byName(doc.scene(), "Cam");
  CHECK(player != nullptr);
  const std::string pg = player->guid;
  const int mi = moverIndex(*player);
  CHECK(mi >= 0);
  doc.select(pg);
  ps.tick(1.0 / 60.0, noPad());
  CHECK(byName(doc.scene(), "Cam")->components[mi].fields.contains("speed"));  // the selected object's live fields

  // edits go to the running World ...
  CHECK(doc.setField(pg, mi, "speed", 3.0));
  ps.tick(1.0 / 60.0, noPad());
  CHECK(byName(doc.scene(), "Cam")->components[mi].fields["speed"].get<double>() == 3.0);
  const float xa = byName(doc.scene(), "Cam")->position[0];
  ps.tick(1.0 / 60.0, noPad());
  CHECK(near(byName(doc.scene(), "Cam")->position[0], xa + 3.0f));  // the script now moves by 3 per step
  const float pos[3] = {40, 2, 3}, rot[4] = {0, 0, 0, 1}, scl[3] = {1, 1, 1};
  CHECK(doc.setTransform(pg, pos, rot, scl));
  CHECK(byName(doc.scene(), "Cam")->position[0] == 40.0f);
  ps.tick(1.0 / 60.0, noPad());
  CHECK(near(byName(doc.scene(), "Cam")->position[0], 43.0f));
  // ... and everything structural is refused, nothing reaches the undo stack
  CHECK(doc.createObject("Nope", "").empty());
  CHECK(!doc.remove(groundGuid));
  CHECK(!doc.undo());
  CHECK(doc.undoDepth() == undoBefore);
  CHECK(doc.document() == docBefore);  // the saved document never changed

  ps.stop(doc);
  CHECK(doc.document() == docBefore);
  CHECK(doc.undoDepth() == undoBefore);
  CHECK(doc.dirty() == dirtyBefore);
  CHECK(doc.selected() == groundGuid);
  CHECK(slurp(scene) == fileBefore);  // the scene file on disk was never touched
  CHECK(doc.canUndo());               // the pre-play rename can still be undone
  CHECK(doc.undo());
}

TEST(play_runtime_objects_are_marked_and_budgets_warn_once) {
  const fs::path proj = makeProject("play_runtime", 50);  // far below the fixture's triangle count
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(proj / "Assets" / "Scenes" / "test.scene", err));
  PlaySession ps;
  CHECK(startOk(ps, doc, proj));
  ps.tick(1.0 / 60.0, noPad());
  CHECK(ps.world()->overBudget());
  bool tri = false;
  for (const BudgetFinding& f : ps.findings()) tri = tri || f.key == "triangles";
  CHECK(tri);
  CHECK(ps.stats().triangles > 50);

  const size_t before = doc.scene().objects.size();
  GameObject* go = ps.world()->world().findByName("Player");
  CHECK(go != nullptr);
  GameObject* clone = ps.world()->world().instantiate(go);
  CHECK(clone != nullptr);
  ps.tick(1.0 / 60.0, noPad());
  CHECK(doc.scene().objects.size() >= before + 1);  // the clone and its children
  const SceneObject* rt = byName(doc.scene(), "Player (Clone) (runtime)");
  CHECK(rt != nullptr);
  CHECK(rt && ps.isRuntimeOnly(rt->guid));
  ps.stop(doc);
  CHECK(byName(doc.scene(), "Player (Clone) (runtime)") == nullptr);
  CHECK(doc.scene().objects.size() == before);
}

TEST(play_blocked_without_scene_and_double_start) {
  SceneDocument empty;
  PlaySession ps;
  std::string err;
  CHECK(!ps.start(empty, fs::path(RENSGA_TEST_TMP_DIR), "psp", err));
  CHECK(!err.empty());
  const fs::path proj = makeProject("play_double");
  SceneDocument doc;
  CHECK(doc.load(proj / "Assets" / "Scenes" / "test.scene", err));
  CHECK(startOk(ps, doc, proj));
  CHECK(!ps.start(doc, proj, "psp", err));
  ps.stop(doc);
}
