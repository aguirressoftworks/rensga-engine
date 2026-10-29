#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

struct TestMover : Behaviour {
  REFLECT(TestMover)
  SERIALIZE_FIELD(Guid, target, Guid())
  SERIALIZE_FIELD(Scalar, speed, 1)
  SERIALIZE_FIELD(i32, lives, 3)
  SERIALIZE_FIELD(bool, flying, false)
};

static CookResult cook(const char* fixture, bool force = true) {
  CookOptions o;
  o.project = fs::path(RENSGA_FIXTURES_DIR) / fixture;
  o.force = force;
  o.genHeader = o.project / "Library" / "Generated" / "console_target.gen.h";
  return cookProject(o, [](const CookMessage& m) { fprintf(stderr, "    [cook] %s\n", m.text.c_str()); });
}

static std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// 16-byte aligned copy (PackView requires it).
struct Aligned {
  Arena arena;
  const u8* data = nullptr;
  size_t size = 0;
  explicit Aligned(const std::vector<uint8_t>& v) {
    arena.create(v.size() + 64);
    u8* p = static_cast<u8*>(arena.allocate(v.size()));
    memcpy(p, v.data(), v.size());
    data = p;
    size = v.size();
  }
};

static GameObject* find(World& w, const char* name) { return w.findByName(name); }

TEST(cook_fixture_and_load_back) {
  CookResult r = cook("cook_ok");
  CHECK(r.ok);
  CHECK(fs::exists(r.packPath));
  CHECK(fs::exists(r.reportPath));
  CHECK(fs::exists(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok/Library/Generated/console_target.gen.h"));

  Aligned pk(readAll(r.packPath));
  PackView view;
  CHECK(view.open(pk.data, pk.size));
  CHECK_EQ(view.header->version, 1u);
  for (u32 i = 0; i < view.header->sectionCount; ++i) CHECK((view.sections[i].offset & 15) == 0);

  Arena arena;
  arena.create(1 << 20);
  World world;
  WorldConfig cfg;
  cfg.maxObjects = 32;
  cfg.maxComponentsPerType = 32;
  CHECK(world.init(cfg, arena));
  LoadedPack lp;
  CHECK(world.loadPack(view, &lp));
  CHECK_EQ(lp.objectCount, 5u);
  CHECK_EQ(lp.componentCount, 7u);
  CHECK_EQ(world.objectCount(), 5u);
  CHECK(lp.hasTarget);
  CHECK_EQ(lp.target.width, 480u);
  CHECK_EQ(lp.target.height, 272u);
  CHECK_EQ(lp.inputCount, 4u);
  CHECK_EQ(lp.meshes.meshCount, 3u);

  GameObject* player = find(world, "Player");
  GameObject* hat = find(world, "Hat");
  GameObject* cam = find(world, "Cam");
  CHECK(player && hat && cam);
  if (!player || !hat || !cam) return;
  CHECK_NEAR(player->transform.localPosition.x, 1.0f, 1e-5f);
  CHECK_NEAR(player->transform.localPosition.z, 3.0f, 1e-5f);
  CHECK(hat->transform.parent == &player->transform);
  CHECK_NEAR(hat->transform.localScale.x, 0.5f, 1e-5f);
  CharacterController* cc = player->GetComponent<CharacterController>();
  CHECK(cc != nullptr);
  if (cc) CHECK_NEAR(cc->gravity, -15.5f, 1e-5f);
  MeshRenderer* mr = player->GetComponent<MeshRenderer>();
  CHECK(mr != nullptr);
  if (mr) {
    CHECK_EQ(mr->color.r, 10);
    CHECK_EQ(mr->color.b, 30);
    CHECK(mr->meshIndex != kInvalidIndex);
    Guid cap;
    Guid::parse("00000000-0000-4000-8000-00000000c003", cap);
    CHECK(mr->mesh == cap);
    if (mr->meshIndex != kInvalidIndex) {
      const PackMeshRec& mesh = lp.meshes.meshes[mr->meshIndex];
      CHECK(mesh.vertexCount > 0 && mesh.vertexCount % 3 == 0);
      CHECK(mesh.objectIndex < 5u);
    }
  }
  CHECK_NEAR(cam->GetComponent<Camera>()->fov, 70.0f, 1e-5f);
  TestMover* tm = cam->GetComponent<TestMover>();
  CHECK(tm != nullptr);
  if (tm) {
    CHECK_NEAR(tm->speed, 3.5f, 1e-5f);
    CHECK_EQ(tm->lives, 7);
    CHECK(tm->flying);
    CHECK(tm->target == player->guid);
  }
}

TEST(cook_is_incremental_and_deterministic) {
  CookResult a = cook("cook_ok", true);
  std::vector<uint8_t> first = readAll(a.packPath);
  CookResult b = cook("cook_ok", false);
  CHECK(b.ok);
  CHECK(b.upToDate);
  CookResult c = cook("cook_ok", true);
  CHECK(!c.upToDate);
  CHECK(readAll(c.packPath) == first);
}

TEST(pack_loader_rejects_version_2) {
  CookResult r = cook("cook_ok");
  std::vector<uint8_t> bytes = readAll(r.packPath);
  Aligned good(bytes);
  PackView v;
  CHECK(v.open(good.data, good.size));
  bytes[4] = 2;  // version field
  Aligned bad(bytes);
  CHECK(!v.open(bad.data, bad.size));
  bytes[4] = 1;
  bytes[0] = 'X';
  Aligned badMagic(bytes);
  CHECK(!v.open(badMagic.data, badMagic.size));
}

TEST(budget_validator_names_asset_guid) {
  CookResult r = cook("cook_over");
  CHECK(!r.ok);
  CHECK(!fs::exists(r.packPath));
  bool found = false;
  for (const CookMessage& m : r.errors)
    if (m.text.find("00000000-0000-4000-8000-00000000d0f1") != std::string::npos &&
        m.text.find("triangles") != std::string::npos && m.assetGuid == "00000000-0000-4000-8000-00000000d0f1" &&
        m.asset == "Assets/Scenes/test.scene")
      found = true;
  CHECK(found);
}

TEST(budget_validator_unit) {
  ConsoleTargetData t;
  t.ramBytes = 100; t.trianglesPerFrame = 10; t.maxObjects = 2; t.maxComponentsPerType = 3;
  SceneBudgetInput in;
  in.sceneAsset = "Assets/Scenes/x.scene";
  in.sceneGuid = "g-1";
  in.packBytes = 101; in.triangles = 11; in.objects = 3;
  in.componentsPerType.push_back({"Pickup", 4});
  std::vector<CookMessage> errs;
  CHECK(!validateBudgets(t, in, errs));
  CHECK_EQ(errs.size(), 4u);
  for (const CookMessage& m : errs) CHECK(m.text.find("g-1") != std::string::npos);
  errs.clear();
  in.packBytes = 100; in.triangles = 10; in.objects = 2; in.componentsPerType[0].count = 3;
  CHECK(validateBudgets(t, in, errs));
}

TEST(console_target_header) {
  ConsoleTargetData t = loadConsoleTarget(fs::path(RENSGA_TEMPLATE_DIR) / "ProjectSettings/ConsoleTarget.json");
  CHECK(t.profile == "psp");
  CHECK_EQ(t.width, 480u);
  CHECK_EQ(t.ramBytes, 25165824u);
  std::string h = generateConsoleTargetHeader(t);
  CHECK(h.find("#define ENGINE_PROFILE_PSP 1") != std::string::npos);
  CHECK(h.find("kScreenWidth = 480") != std::string::npos);
}
