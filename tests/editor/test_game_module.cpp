// F3.03: the game script module. A temp project with a Rotator-like script is built into a shared library,
// loaded, edited, rebuilt and hot-reloaded while a World holds live state. Needs python3 and a C++ compiler
// (the same tools the main build needs). The template project's module is built once by the
// editor_script_module_template fixture and also checked here.
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "rensga/Rensga.h"
#include "rensga/core/reflect_json.h"
#include "rensga/editor/game_module.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path freshProject(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "Assets" / "Scripts");
  return d;
}

void writeScript(const fs::path& root, const std::string& text) {
  std::ofstream o(root / "Assets" / "Scripts" / "Rotator.cpp", std::ios::binary | std::ios::trunc);
  o << text;
}

const char* kRotatorV1 = R"(#include <rensga/Rensga.h>
using namespace rensga;
struct Rotator : Behaviour {
  REFLECT(Rotator)
  SERIALIZE_FIELD(Scalar, speed, 10.0f)
  void Update() override { transform()->Translate(Vector3(speed, 0, 0), Space::World); }
};
)";

const char* kRotatorV2 = R"(#include <rensga/Rensga.h>
using namespace rensga;
struct Rotator : Behaviour {
  REFLECT(Rotator)
  SERIALIZE_FIELD(Scalar, speed, 20.0f)
  SERIALIZE_FIELD(Scalar, boost, 2.0f)
  void Update() override { transform()->Translate(Vector3(speed * boost, 0, 0), Space::World); }
};
)";

size_t shadowFiles(const fs::path& root) {
  std::error_code ec;
  size_t n = 0;
  for (fs::directory_iterator it(root / "Library" / "ScriptAssemblies" / "shadow", ec), end; !ec && it != end; it.increment(ec)) ++n;
  return n;
}

struct Sim {
  Arena arena;
  World world;
  GameObject* go = nullptr;
  Sim(const char* guid) {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
    go = world.createObject("rot");
    Guid::parse(guid, go->guid);
  }
  ~Sim() { world.shutdown(); }
};

}  // namespace

TEST(script_module_layout_and_stamp) {
  fs::path root = freshProject("gm_layout");
  const ScriptModuleLayout l = scriptModuleLayout(root);
  CHECK(l.output.filename().string().find("rensga_game") != std::string::npos);
  CHECK(l.buildDir == root / "Library" / "ScriptBuild");
  CHECK_EQ(l.commands.size(), 2u);
  CHECK(!projectHasScripts(root));
  CHECK(!scriptModuleUpToDate(root));
  writeScript(root, kRotatorV1);
  CHECK(projectHasScripts(root));
}

TEST(script_module_build_load_reload_keeps_state) {
  const fs::path root = freshProject("gm_hot");
  writeScript(root, kRotatorV1);
  std::string log;
  const bool built = buildScriptModuleBlocking(root, log);
  if (!built) fprintf(stderr, "%s\n", log.c_str());
  CHECK(built);
  CHECK(scriptModuleUpToDate(root));

  GameModule& gm = GameModule::instance();
  CHECK(!gm.loaded());
  const ScriptModuleLayout l = scriptModuleLayout(root);
  std::string err;
  CHECK(gm.load(l.output, err));
  if (!gm.loaded()) { fprintf(stderr, "load: %s\n", err.c_str()); return; }
  CHECK_EQ(gm.generation(), 1);
  CHECK_EQ(gm.types().size(), 1u);

  const TypeInfo* ti = TypeRegistry::instance().find("Rotator");
  CHECK(ti != nullptr);
  CHECK(ti && ti->isBehaviour());
  if (!ti) { gm.unload(); return; }
  CHECK_EQ(ti->fieldCount, 1u);
  CHECK(gm.ownsType(ti));
  {
    json d;
    void* mem = ::operator new(ti->size, std::align_val_t(ti->align));
    ti->construct(mem);
    d = fieldsToJson(*ti, mem);
    ti->destruct(mem);
    ::operator delete(mem, std::align_val_t(ti->align));
    CHECK_NEAR(d["speed"].get<float>(), 10.0f, 0.001f);
  }

  // Live state: speed was changed at runtime (inspector or script), then the source is edited.
  json saved;
  const char* kGuid = "00000000-0000-4000-8000-0000000000c1";
  {
    Sim sim(kGuid);
    Component* c = sim.world.addComponent(sim.go, *ti);
    CHECK(c != nullptr);
    sim.world.startPending();
    static_cast<Behaviour*>(ti->asBehaviour(c))->Update();  // v1: +speed
    CHECK_NEAR(sim.go->transform.position().x, 10.0f, 0.001f);
    json one = {{"speed", 7.0f}};
    fieldsFromJson(*ti, c, one);
    saved = serializeScriptState(sim.world);
    CHECK_EQ(saved.size(), 1u);
    // The world must not outlive the old types.
  }

  fs::path oldShadow = gm.shadowPath();
  CHECK(fs::exists(oldShadow));
  // The edited source must be newer than the first build, whatever the file system's time resolution.
  fs::last_write_time(l.output, fs::file_time_type::clock::now() - std::chrono::seconds(10));
  writeScript(root, kRotatorV2);
  CHECK(!scriptModuleUpToDate(root));
  log.clear();
  CHECK(buildScriptModuleBlocking(root, log));
  CHECK(scriptModuleUpToDate(root));

  int before = 0, after = 0;
  gm.addBeforeUnload([&] { ++before; });
  gm.addAfterLoad([&] { ++after; });
  CHECK(gm.reload(l.output, err));
  CHECK_EQ(gm.generation(), 2);
  CHECK_EQ(before, 1);
  CHECK_EQ(after, 1);
  CHECK(!fs::exists(oldShadow));
  CHECK_EQ(shadowFiles(root), 1u);
  CHECK_EQ(gm.types().size(), 1u);

  ti = TypeRegistry::instance().find("Rotator");
  CHECK(ti != nullptr);
  if (ti) {
    CHECK_EQ(ti->fieldCount, 2u);
    Sim sim(kGuid);
    Component* c = sim.world.addComponent(sim.go, *ti);
    CHECK(c != nullptr);
    json fresh = fieldsToJson(*ti, c);
    CHECK_NEAR(fresh["speed"].get<float>(), 20.0f, 0.001f);  // new default before the restore
    restoreScriptState(sim.world, saved);
    json now = fieldsToJson(*ti, c);
    CHECK_NEAR(now["speed"].get<float>(), 7.0f, 0.001f);   // the existing field survived
    CHECK_NEAR(now["boost"].get<float>(), 2.0f, 0.001f);   // the new field has its default
    sim.world.startPending();
    static_cast<Behaviour*>(ti->asBehaviour(c))->Update();  // v2: +speed*boost
    CHECK_NEAR(sim.go->transform.position().x, 14.0f, 0.001f);
  }

  gm.unload();
  CHECK(!gm.loaded());
  CHECK(TypeRegistry::instance().find("Rotator") == nullptr);
  CHECK_EQ(shadowFiles(root), 0u);
}

TEST(script_module_failed_reload_keeps_previous) {
  const fs::path root = freshProject("gm_fail");
  writeScript(root, kRotatorV1);
  std::string log, err;
  CHECK(buildScriptModuleBlocking(root, log));
  GameModule& gm = GameModule::instance();
  const ScriptModuleLayout l = scriptModuleLayout(root);
  CHECK(gm.load(l.output, err));
  // A broken library file does not load; the old module keeps running.
  const fs::path bad = root / "Library" / "ScriptAssemblies" / "bad.so";
  std::ofstream(bad) << "not a library";
  CHECK(!gm.reload(bad, err));
  CHECK(!err.empty());
  CHECK(gm.loaded());
  CHECK(TypeRegistry::instance().find("Rotator") != nullptr);
  gm.unload();
  CHECK(TypeRegistry::instance().find("Rotator") == nullptr);
}

TEST(script_module_build_error_reports_diagnostics) {
  const fs::path root = freshProject("gm_err");
  writeScript(root, "#include <rensga/Rensga.h>\nint boom() { return missing_symbol; }\n");
  std::string log;
  CHECK(!buildScriptModuleBlocking(root, log));
  CHECK(log.find("missing_symbol") != std::string::npos);
}

// The template project's own scripts as a module: what `rensga-editor --project templates/psp-3d` loads.
TEST(script_module_template_build) {
  const fs::path root = RENSGA_TEMPLATE_DIR;
  std::string log, err;
  if (!scriptModuleUpToDate(root)) {
    const bool ok = buildScriptModuleBlocking(root, log);
    if (!ok) fprintf(stderr, "%s\n", log.c_str());
    CHECK(ok);
  }
  GameModule& gm = GameModule::instance();
  CHECK(gm.load(scriptModuleLayout(root).output, err));
  if (!gm.loaded()) { fprintf(stderr, "load: %s\n", err.c_str()); return; }
  for (const char* n : {"PlayerController", "Pickup", "FollowCamera"})
    CHECK(TypeRegistry::instance().find(n) != nullptr);
  gm.unload();
}
