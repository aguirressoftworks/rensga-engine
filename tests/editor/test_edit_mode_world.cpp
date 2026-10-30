// F3.07 (API 11): edit-mode World, gizmo callbacks, EXECUTE_IN_EDIT_MODE, and the Patrol exercise of samples/editor_tour.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "rensga/Rensga.h"
#include "rensga/core/gizmos.h"
#include "rensga/editor/edit_mode_world.h"
#include "rensga/editor/inspector_model.h"
#include "rensga/editor/scene_document.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

namespace emtest {
int awakes = 0, starts = 0, updates = 0, destroys = 0, dormantAwakes = 0, dormantUpdates = 0;
}

struct EditProbe : Behaviour {
  REFLECT(EditProbe)
  EXECUTE_IN_EDIT_MODE()
  SERIALIZE_FIELD(Scalar, size, 1.0f)
  void Awake() override { emtest::awakes++; }
  void Start() override { emtest::starts++; }
  void Update() override { emtest::updates++; }
  void OnDestroy() override { emtest::destroys++; }
  void OnDrawGizmos() override { Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(size, 0, 0)); }
  void OnDrawGizmosSelected() override { Gizmos::DrawWireCube(Vector3(0, 0, 0), Vector3(size, size, size)); }
};

struct EditDormant : Behaviour {
  REFLECT(EditDormant)
  void Awake() override { emtest::dormantAwakes++; }
  void Update() override { emtest::dormantUpdates++; }
  void OnDrawGizmos() override { Gizmos::DrawLine(Vector3(0, 1, 0), Vector3(0, 2, 0)); }
};

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream i(p, std::ios::binary);
  std::ostringstream s;
  s << i.rdbuf();
  return s.str();
}

fs::path freshCopy(const fs::path& src, const char* name) {
  const fs::path dst = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst.parent_path());
  fs::copy(src, dst, fs::copy_options::recursive);
  fs::remove_all(dst / "Library");
  return dst;
}

void allow(const fs::path& proj, std::initializer_list<const char*> types) {
  const fs::path ct = proj / "ProjectSettings" / "ConsoleTarget.json";
  json j = json::parse(slurp(ct));
  for (const char* t : types) j["allowedComponents"].push_back(t);
  std::ofstream(ct, std::ios::binary | std::ios::trunc) << j.dump(2);
}

// cook_ok where Cam carries an EditProbe and Player an EditDormant.
fs::path probeProject(const char* name) {
  const fs::path dst = freshCopy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok", name);
  const fs::path scene = dst / "Assets" / "Scenes" / "test.scene";
  std::string txt = slurp(scene);
  for (size_t p; (p = txt.find("\"TestMover\"")) != std::string::npos;) txt.replace(p, 11, "\"EditProbe\"");
  json j = json::parse(txt);
  for (auto& o : j["objects"])
    for (auto& c : o["components"])
      if (c["type"] == "EditProbe") c["fields"] = {{"size", 1.0}};
  for (auto& o : j["objects"])
    if (o["name"] == "Player") o["components"].push_back({{"type", "EditDormant"}, {"fields", json::object()}});
  std::ofstream(scene, std::ios::binary | std::ios::trunc) << j.dump(1);
  allow(dst, {"EditProbe", "EditDormant"});
  return dst;
}

std::string guidOf(const SceneDocument& d, const char* name) {
  for (const SceneObject& o : d.scene().objects) if (o.name == name) return o.guid;
  return std::string();
}

void resetCounters() { emtest::awakes = emtest::starts = emtest::updates = emtest::destroys = emtest::dormantAwakes = emtest::dormantUpdates = 0; }

}  // namespace

TEST(edit_mode_world_runs_only_execute_in_edit_mode_behaviours) {
  const fs::path proj = probeProject("em_probe");
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(proj / "Assets/Scenes/test.scene", err));
  const ordered_json before = doc.document();
  resetCounters();
  {
    EditModeWorld ew;
    ew.setRequireModule(false);
    CHECK(ew.sync(doc, proj, "psp"));
    CHECK(ew.valid());
    CHECK(!Application::isPlaying());
    CHECK_EQ(emtest::awakes, 1);
    CHECK_EQ(emtest::starts, 1);
    CHECK_EQ(emtest::updates, 1);                 // one Update pass after the build
    CHECK_EQ(emtest::dormantAwakes, 0);           // no EXECUTE_IN_EDIT_MODE: dormant
    CHECK_EQ(emtest::dormantUpdates, 0);
    CHECK(ew.sync(doc, proj, "psp"));             // nothing changed: no rebuild
    CHECK_EQ(ew.rebuilds(), 1u);

    // a document change rebuilds after the debounce, and runs Awake / Start / Update again on the new World
    ew.setDebounce(0.25);
    const std::string cam = guidOf(doc, "Cam");
    CHECK(doc.setField(cam, 1, "size", 3.0));
    ew.update(doc, proj, "psp", 0.05, false);
    ew.update(doc, proj, "psp", 0.15, false);     // the change is noticed here
    CHECK_EQ(ew.rebuilds(), 1u);                  // still inside the debounce
    ew.update(doc, proj, "psp", 0.30, false);
    CHECK_EQ(ew.rebuilds(), 2u);
    CHECK_EQ(emtest::awakes, 2);
    CHECK_EQ(emtest::updates, 2);
    CHECK(emtest::destroys >= 1);                 // the old World went through OnDestroy

    // suspended (Play session): the World is released
    ew.update(doc, proj, "psp", 0.05, true);
    CHECK(!ew.valid());
  }
  // the saved document was never touched by the edit-mode World (only by the setField above)
  CHECK(doc.document() != before);
  ordered_json expect = before;
  for (auto& o : expect["objects"])
    if (o["name"] == "Cam") o["components"][1]["fields"]["size"] = 3.0;
  CHECK(doc.document() == expect);
}

TEST(edit_mode_world_draws_gizmos_into_the_list) {
  const fs::path proj = probeProject("em_gizmos");
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(proj / "Assets/Scenes/test.scene", err));
  EditModeWorld ew;
  ew.setRequireModule(false);
  CHECK(ew.sync(doc, proj, "psp"));

  GizmoDraw::clear();
  ew.drawGizmos("");  // nothing selected: OnDrawGizmos of both behaviours
  CHECK_EQ(GizmoDraw::lineCount(), 2u);
  const unsigned plain = GizmoDraw::lineCount();

  GizmoDraw::clear();
  ew.drawGizmos(guidOf(doc, "Cam"));  // OnDrawGizmosSelected adds a wire cube (12 lines)
  CHECK(GizmoDraw::lineCount() > plain);

  GizmoDraw::clear();
  ew.drawGizmos(guidOf(doc, "Player"));  // EditDormant has no selected callback
  CHECK_EQ(GizmoDraw::lineCount(), plain);

  // a disabled behaviour does not draw
  CHECK(doc.setActive(guidOf(doc, "Player"), false));
  ew.setDebounce(0);
  ew.update(doc, proj, "psp", 0.5, false);
  GizmoDraw::clear();
  ew.drawGizmos("");
  CHECK_EQ(GizmoDraw::lineCount(), 1u);
  GizmoDraw::clear();
}

// ---- the Patrol exercise of samples/editor_tour -------------------------------------------------------------------

namespace {

fs::path patrolProject(const char* name) {
  const fs::path dst = freshCopy(fs::path(RENSGA_TOUR_DIR), name);
  const fs::path pj = dst / "ProjectSettings" / "Project.json";
  json j = json::parse(slurp(pj));
  j["scenes"] = json::array({"00000000-0000-4000-a000-00000000e1a3"});  // the Patrol scene only (the others use other scripts)
  std::ofstream(pj, std::ios::binary | std::ios::trunc) << j.dump(2);
  return dst;
}

bool hasLine(const Vector3& a, const Vector3& b) {
  for (unsigned i = 0; i < GizmoDraw::lineCount(); ++i) {
    const GizmoLine& l = GizmoDraw::lines()[i];
    const bool fwd = l.a[0] == a.x && l.a[1] == a.y && l.a[2] == a.z && l.b[0] == b.x && l.b[1] == b.y && l.b[2] == b.z;
    if (fwd) return true;
  }
  return false;
}

}  // namespace

TEST(patrol_gizmos_show_the_route) {
  const fs::path proj = patrolProject("em_patrol");
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(proj / "Assets/Scenes/Patrol.scene", err));
  EditModeWorld ew;
  ew.setRequireModule(false);
  CHECK(ew.sync(doc, proj, "psp"));
  CHECK(ew.valid());

  GizmoDraw::clear();
  ew.drawGizmos("");
  // 3 waypoints in a loop: the three segments are in the draw list, plus the wire spheres
  CHECK(hasLine(Vector3(-3, 0.5f, -2), Vector3(3, 0.5f, -2)));
  CHECK(hasLine(Vector3(3, 0.5f, -2), Vector3(0, 0.5f, 3)));
  CHECK(hasLine(Vector3(0, 0.5f, 3), Vector3(-3, 0.5f, -2)));
  CHECK(GizmoDraw::lineCount() > 3u);
  const unsigned plain = GizmoDraw::lineCount();

  GizmoDraw::clear();
  ew.drawGizmos(guidOf(doc, "Guard"));  // selected: the next stop and a box around the guard
  CHECK(GizmoDraw::lineCount() > plain);
  GizmoDraw::clear();
}

TEST(patrol_on_validate_clamps_through_the_inspector) {
  const fs::path proj = patrolProject("em_patrol_validate");
  SceneDocument doc;
  std::string err;
  CHECK(doc.load(proj / "Assets/Scenes/Patrol.scene", err));
  const std::string g = guidOf(doc, "Guard");
  int idx = -1;
  const SceneObject* o = doc.find(g);
  for (size_t i = 0; o && i < o->components.size(); ++i) if (o->components[i].type == "Patrol") idx = static_cast<int>(i);
  CHECK(idx >= 0);
  const ComponentRef r{g, idx};

  CHECK(editFields(doc, {r}, "waypointCount", 8));     // inside the RANGE, past what Patrol supports
  CHECK_EQ(fieldValue(doc, r, "waypointCount").get<int>(), 4);
  CHECK(editFields(doc, {r}, "waypointCount", 0));
  CHECK_EQ(fieldValue(doc, r, "waypointCount").get<int>(), 2);
  CHECK(editFields(doc, {r}, "speed", 0.0));
  CHECK_NEAR(fieldValue(doc, r, "speed").get<double>(), 0.25, 1e-6);
  CHECK(editFields(doc, {r}, "waypoint1", json::array({3, -2, -2})));
  CHECK_NEAR(fieldValue(doc, r, "waypoint1")[1].get<double>(), 0.0, 1e-6);

  // the change shows in the edit-mode World: two waypoints, one segment each way
  EditModeWorld ew;
  ew.setRequireModule(false);
  CHECK(ew.sync(doc, proj, "psp"));
  GizmoDraw::clear();
  ew.drawGizmos("");
  CHECK(hasLine(Vector3(-3, 0.5f, -2), Vector3(3, 0.0f, -2)));
  CHECK(hasLine(Vector3(3, 0.0f, -2), Vector3(-3, 0.5f, -2)));
  GizmoDraw::clear();
}
