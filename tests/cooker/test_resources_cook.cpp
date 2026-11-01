// F6.11 (A3): the RSRC section (Resources.Load names) and Project.json "additiveScenes" in a cooked pack. Works on a temp copy of
// the assets2d fixture whose sound and texture are moved under Assets/Resources/.
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/Rensga.h"
#include "rensga/Resources.h"
#include "rensga/cooker/cooker.h"
#include "rensga/core/scene_manager.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

namespace {
fs::path makeProject(const char* tag) {
  const fs::path src = fs::path(RENSGA_FIXTURES_DIR) / "assets2d";
  const fs::path dst = fs::temp_directory_path() / (std::string("rensga_rsrc_") + tag);
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst / "Assets", ec);
  fs::copy(src / "ProjectSettings", dst / "ProjectSettings", fs::copy_options::recursive, ec);
  fs::copy(src / "Assets", dst / "Assets", fs::copy_options::recursive, ec);
  fs::create_directories(dst / "Assets" / "Resources" / "Sfx", ec);
  fs::create_directories(dst / "Assets" / "Resources" / "ui", ec);
  fs::rename(dst / "Assets/Audio/sfx.wav", dst / "Assets/Resources/Sfx/Boing.wav");
  fs::rename(dst / "Assets/Audio/sfx.wav.meta", dst / "Assets/Resources/Sfx/Boing.wav.meta");
  fs::rename(dst / "Assets/Tiles/noisy.png", dst / "Assets/Resources/ui/Noisy.png");
  fs::rename(dst / "Assets/Tiles/noisy.png.meta", dst / "Assets/Resources/ui/Noisy.png.meta");
  return dst;
}
std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
std::string readText(const fs::path& p) {
  std::vector<uint8_t> b = readAll(p);
  return std::string(b.begin(), b.end());
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
Guid guidOf(int n) {
  char t[64];
  snprintf(t, sizeof t, "00000000-0000-4000-8000-0000000a2d%02x", n);
  Guid g;
  Guid::parse(t, g);
  return g;
}
}  // namespace

TEST(cook_rsrc_lists_assets_under_resources_sorted_by_hash) {
  const fs::path proj = makeProject("a");
  CookResult r = cook(proj);
  CHECK(r.ok);
  std::vector<uint8_t> bytes = readAll(r.packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(mem, bytes.size(), "rsrc"));
  PackContent c = view.content();
  CHECK_EQ(c.resourceCount, 2u);
  const PackResource* boing = c.findResource(packResourceNameHash("sfx/boing"));
  const PackResource* noisy = c.findResource(packResourceNameHash("UI/noisy"));
  CHECK(boing != nullptr && noisy != nullptr);
  if (boing && noisy) {
    CHECK_EQ(boing->kind, static_cast<u32>(PackResourceKind::Audio));
    CHECK_EQ(noisy->kind, static_cast<u32>(PackResourceKind::Texture));
    CHECK(strcmp(view.string(boing->nameStr), "Sfx/Boing") == 0);  // the stored name keeps its case, the hash does not
    CHECK(noisy->guid == guidOf(2));
    CHECK(boing->guid == guidOf(3));
    // the same pack answers Resources::Load
    SceneManager::bind(&view, 0);
    Handle<ResAudioClip> h = Resources::Load<ResAudioClip>("sfx/boing");
    CHECK(h.valid() && h.guid == boing->guid);
    CHECK(!Resources::Load<ResTexture>("sfx/boing").valid());
    Handle<ResTexture> t = Resources::Load<ResTexture>("ui/noisy");
    CHECK(t.valid());
    // and the asset really is in the pack
    CHECK(view.assets().findTexture(t.guid) != nullptr);
    CHECK(view.assets().findAudio(h.guid) != nullptr);
    Resources::Unload(h);
    Resources::Unload(t);
    SceneManager::unbind();
  }
  for (u32 i = 1; i < c.resourceCount; ++i) CHECK(c.resources[i - 1].nameHash <= c.resources[i].nameHash);
  // cooking twice gives identical bytes
  CookResult r2 = cook(proj);
  CHECK(r2.ok && readAll(r2.packPath) == bytes);
}

TEST(cook_without_a_resources_folder_writes_no_rsrc) {
  const fs::path src = fs::path(RENSGA_FIXTURES_DIR) / "assets2d";
  const fs::path dst = fs::temp_directory_path() / "rensga_rsrc_none";
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst, ec);
  fs::copy(src / "ProjectSettings", dst / "ProjectSettings", fs::copy_options::recursive, ec);
  fs::copy(src / "Assets", dst / "Assets", fs::copy_options::recursive, ec);
  CookResult r = cook(dst);
  CHECK(r.ok);
  std::vector<uint8_t> bytes = readAll(r.packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(mem, bytes.size(), "none"));
  CHECK(view.find(pack_section::RSRC) == nullptr);
}

TEST(cook_rsrc_refuses_two_assets_with_the_same_resource_name) {
  const fs::path proj = makeProject("dup");
  // a second texture whose Resources name differs only by case
  fs::copy_file(proj / "Assets/Resources/ui/Noisy.png", proj / "Assets/Resources/ui/NOISY.png");
  nlohmann::json meta = nlohmann::json::parse(readText(proj / "Assets/Resources/ui/Noisy.png.meta"));
  meta["guid"] = "00000000-0000-4000-8000-0000000a2dff";
  std::ofstream(proj / "Assets/Resources/ui/NOISY.png.meta") << meta.dump(1);
  std::string err;
  CookResult r = cook(proj, &err);
  CHECK(!r.ok);
  CHECK(err.find("ui/NOISY") != std::string::npos || err.find("NOISY") != std::string::npos);
}

TEST(cook_marks_additive_scenes_from_project_json) {
  const fs::path proj = makeProject("add");
  const fs::path pj = proj / "ProjectSettings/Project.json";
  nlohmann::json j = nlohmann::json::parse(readText(pj));
  j["additiveScenes"] = nlohmann::json::array({j["scenes"][0]});
  std::ofstream(pj) << j.dump(1);
  CookResult r = cook(proj);
  CHECK(r.ok);
  std::vector<uint8_t> bytes = readAll(r.packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(mem, bytes.size(), "add"));
  u32 n = 0;
  const PackSceneRec* sc = view.records<PackSceneRec>(pack_section::SCNE, &n);
  CHECK(sc != nullptr && n == 1 && (sc[0].reserved & kSceneAdditive) != 0);
  // an unknown GUID is an error that names it
  j["additiveScenes"] = nlohmann::json::array({"00000000-0000-4000-8000-00000000dead"});
  std::ofstream(pj) << j.dump(1);
  std::string err;
  CookResult bad = cook(proj, &err);
  CHECK(!bad.ok && err.find("dead") != std::string::npos);
}

TEST(cook_writes_pcap_from_component_reserve) {
  const fs::path proj = makeProject("pcap");
  const fs::path pj = proj / "ProjectSettings/Project.json";
  nlohmann::json j = nlohmann::json::parse(readText(pj));
  j["componentReserve"] = {{"UIText", 12}, {"UIImage", 30}};
  std::ofstream(pj) << j.dump(1);
  CookResult r = cook(proj);
  CHECK(r.ok);
  std::vector<uint8_t> bytes = readAll(r.packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  CHECK(view.open(mem, bytes.size(), "pcap"));
  u32 n = 0;
  const PackPoolReserve* res = view.records<PackPoolReserve>(pack_section::PCAP, &n);
  CHECK(res != nullptr && n == 2);
  if (res && n == 2) {
    u32 total = 0;
    for (u32 i = 0; i < n; ++i) total += res[i].extra;
    CHECK_EQ(total, 42u);
    CHECK(res[0].typeHash == fnv1a32("UIImage") || res[1].typeHash == fnv1a32("UIImage"));
  }
  j["componentReserve"] = {{"NoSuchType", 3}};
  std::ofstream(pj) << j.dump(1);
  std::string err;
  CookResult bad = cook(proj, &err);
  CHECK(!bad.ok && err.find("NoSuchType") != std::string::npos);
  j["componentReserve"] = {{"UIText", 0}};
  std::ofstream(pj) << j.dump(1);
  err.clear();
  CookResult bad2 = cook(proj, &err);
  CHECK(!bad2.ok && err.find("UIText") != std::string::npos);
}
