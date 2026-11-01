// F6.07 (D3, Editor 13): data assets (*.asset.json) cooked into DATA, read back through DataAssets::Get<T> and Resources::Load<T>.
// Works on a temp copy of the assets2d fixture.
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/DataAsset.h"
#include "rensga/Rensga.h"
#include "rensga/Resources.h"
#include "rensga/cooker/cooker.h"
#include "rensga/cooker/data_cook.h"
#include "rensga/core/scene_manager.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

struct CookStats : DataAsset {
  REFLECT(CookStats)
  SERIALIZE_FIELD(i32, health, 30)
  RANGE(0, 20)
  SERIALIZE_FIELD(Scalar, speed, 3.0f)
  SERIALIZE_FIELD(String32, label, "Grunt")
  SERIALIZE_FIELD(Vector3, spawn, Vector3(0, 1, 0))
  SERIALIZE_ARRAY(i32, waves, 4)
};
struct CookOtherStats : DataAsset {
  REFLECT(CookOtherStats)
  SERIALIZE_FIELD(i32, a, 1)
};

namespace {
const char* kGuidA = "00000000-0000-4000-8000-0000000d3a01";
const char* kGuidB = "00000000-0000-4000-8000-0000000d3a02";

fs::path makeProject(const char* tag) {
  const fs::path src = fs::path(RENSGA_FIXTURES_DIR) / "assets2d";
  const fs::path dst = fs::temp_directory_path() / (std::string("rensga_data_") + tag);
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst / "Assets", ec);
  fs::copy(src / "ProjectSettings", dst / "ProjectSettings", fs::copy_options::recursive, ec);
  fs::copy(src / "Assets", dst / "Assets", fs::copy_options::recursive, ec);
  return dst;
}
void writeAsset(const fs::path& dir, const char* file, const char* guid, const nlohmann::json& doc) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::ofstream(dir / file) << doc.dump(1);
  std::ofstream(dir / (std::string(file) + ".meta")) << "{\"guid\":\"" << guid << "\",\"importer\":\"dataasset\"}";
}
std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
CookResult cook(const fs::path& project, std::string* err = nullptr) {
  CookOptions o;
  o.project = project;
  o.target = "linux";
  o.force = true;
  return cookProject(o, [&](const CookMessage& m) {
    if (m.severity == CookMessage::Error && err && err->empty()) *err = m.text;
  });
}
Guid guid(const char* t) {
  Guid g;
  Guid::parse(t, g);
  return g;
}
}  // namespace

TEST(data_cook_writes_data_and_runtime_reads_it) {
  const fs::path proj = makeProject("a");
  writeAsset(proj / "Assets/Data", "Grunt.asset.json", kGuidA,
             {{"format", 1}, {"kind", "DataAsset"}, {"type", "CookStats"},
              {"fields", {{"health", 12}, {"speed", 4.5}, {"label", "Brute"}, {"spawn", {1, 2, 3}}, {"waves", {5, 6}}}}});
  writeAsset(proj / "Assets/Resources/data", "Boss.asset.json", kGuidB, {{"format", 1}, {"kind", "DataAsset"}, {"type", "CookStats"}, {"fields", {{"health", 99}}}});
  std::string err;
  CookResult r = cook(proj, &err);
  CHECK(r.ok);
  if (!r.ok) std::printf("cook error: %s\n", err.c_str());
  std::vector<uint8_t> bytes = readAll(r.packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(mem, bytes.size(), "data"));
  PackContent c = view.content();
  CHECK_EQ(c.dataCount, 2u);
  SceneManager::bind(&view, 0);
  CHECK_EQ(DataAssets::count(), 2u);
  const CookStats* g = DataAssets::Get<CookStats>(guid(kGuidA));
  CHECK(g != nullptr);
  if (g) {
    CHECK_EQ(g->health, 12);
    CHECK(g->speed == 4.5f);
    CHECK(strcmp(g->label.c_str(), "Brute") == 0);
    CHECK(g->spawn.x == 1 && g->spawn.y == 2 && g->spawn.z == 3);
    CHECK_EQ(g->waves.count, 2u);
    CHECK_EQ(g->waves.items[1], 6);
  }
  CHECK(DataAssets::Get<CookStats>(guid(kGuidA)) == g);                 // same instance, no rebuild
  CHECK(DataAssets::Get<CookOtherStats>(guid(kGuidA)) == nullptr);      // another type
  CHECK(DataAssets::Get<CookStats>(guid("00000000-0000-4000-8000-0000000d3aff")) == nullptr);
  CHECK(DataAssets::Get<CookStats>(Guid()) == nullptr);
  const CookStats* boss = DataAssets::Get<CookStats>(guid(kGuidB));
  CHECK(boss != nullptr && boss->health == 99 && boss->speed == 3.0f && strcmp(boss->label.c_str(), "Grunt") == 0);
  // Resources.Load<T> finds the one under a Resources folder
  Handle<CookStats> h = Resources::Load<CookStats>("data/boss");
  CHECK(h.valid());
  CHECK(h.get() != nullptr && h->health == 99);
  Resources::Unload(h);
  SceneManager::unbind();
  // a second cook is byte-identical
  CookResult r2 = cook(proj);
  CHECK(r2.ok && readAll(r2.packPath) == bytes);
}

TEST(data_cook_changed_values_are_not_served_stale) {
  const fs::path proj = makeProject("b");
  writeAsset(proj / "Assets/Data", "A.asset.json", kGuidA, {{"format", 1}, {"kind", "DataAsset"}, {"type", "CookStats"}, {"fields", {{"health", 1}}}});
  std::vector<std::vector<uint8_t>> packs;
  const CookStats* seen[2] = {nullptr, nullptr};
  i32 health[2] = {0, 0};
  for (int i = 0; i < 2; ++i) {
    writeAsset(proj / "Assets/Data", "A.asset.json", kGuidA,
               {{"format", 1}, {"kind", "DataAsset"}, {"type", "CookStats"}, {"fields", {{"health", i == 0 ? 7 : 8}}}});
    CookResult r = cook(proj);
    CHECK(r.ok);
    packs.push_back(readAll(r.packPath));
    PackView* view = new PackView();
    u8* mem = static_cast<u8*>(aligned_alloc(16, (packs.back().size() + 15) & ~size_t(15)));
    memcpy(mem, packs.back().data(), packs.back().size());
    CHECK(view->open(mem, packs.back().size(), "stale"));
    SceneManager::bind(view, 0);
    seen[i] = DataAssets::Get<CookStats>(guid(kGuidA));
    health[i] = seen[i] ? seen[i]->health : -1;
    SceneManager::unbind();
    delete view;
    free(mem);
  }
  CHECK_EQ(health[0], 7);
  CHECK_EQ(health[1], 8);
}

TEST(data_cook_problems_name_the_asset) {
  const fs::path proj = makeProject("c");
  writeAsset(proj / "Assets/Data", "Bad.asset.json", kGuidA, {{"format", 1}, {"kind", "DataAsset"}, {"type", "CookStats"}, {"fields", {{"hp", 1}}}});
  std::string err;
  CookResult r = cook(proj, &err);
  CHECK(!r.ok);
  CHECK(err.find("hp") != std::string::npos);
  writeAsset(proj / "Assets/Data", "Bad.asset.json", kGuidA, {{"format", 1}, {"kind", "DataAsset"}, {"type", "NoSuchType"}});
  err.clear();
  r = cook(proj, &err);
  CHECK(!r.ok && err.find("NoSuchType") != std::string::npos);
  writeAsset(proj / "Assets/Data", "Bad.asset.json", kGuidA, {{"format", 1}, {"kind", "DataAsset"}, {"type", "Rigidbody"}});
  err.clear();
  r = cook(proj, &err);
  CHECK(!r.ok && err.find("component") != std::string::npos);
  writeAsset(proj / "Assets/Data", "Bad.asset.json", kGuidA, {{"format", 1}, {"kind", "DataAsset"}});
  err.clear();
  r = cook(proj, &err);
  CHECK(!r.ok && err.find("type") != std::string::npos);
}

TEST(data_cook_without_data_assets_writes_no_data_section) {
  const fs::path proj = makeProject("d");
  CookResult r = cook(proj);
  CHECK(r.ok);
  std::vector<uint8_t> bytes = readAll(r.packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(mem, bytes.size(), "nodata"));
  CHECK(view.find(pack_section::DATA) == nullptr);
}

TEST(data_asset_types_are_the_reflected_non_components) {
  bool stats = false, other = false, rb = false;
  for (const TypeInfo* t : cooker::dataAssetTypes()) {
    if (strcmp(t->name, "CookStats") == 0) stats = true;
    if (strcmp(t->name, "CookOtherStats") == 0) other = true;
    if (strcmp(t->name, "Rigidbody") == 0) rb = true;
  }
  CHECK(stats && other && !rb);
}
