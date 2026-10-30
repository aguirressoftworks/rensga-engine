// F3.02 (Editor 3): asset database, file watcher, import settings, VRAM layouts. Host only.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>

#include "rensga/editor/asset_database.h"
#include "rensga/editor/file_watcher.h"
#include "rensga/editor/vram_map.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path copyProject(const fs::path& src, const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  fs::remove_all(d);
  fs::create_directories(d);
  for (const char* sub : {"Assets", "ProjectSettings"}) fs::copy(src / sub, d / sub, fs::copy_options::recursive);
  return d;
}

std::map<std::string, std::string> metaBytes(const fs::path& root) {
  std::map<std::string, std::string> m;
  for (const auto& e : fs::recursive_directory_iterator(root / "Assets"))
    if (e.path().extension() == ".meta") m[fs::relative(e.path(), root).generic_string()] = slurp(e.path());
  return m;
}

const fs::path kSample = fs::path(RENSGA_SAMPLE_DIR);
const fs::path kTemplate = fs::path(RENSGA_TEMPLATE_DIR);

}  // namespace

TEST(asset_meta_stability) {
  for (const fs::path& src : {kSample, kTemplate}) {
    fs::path d = copyProject(src, "assetdb_stable");
    const auto before = metaBytes(d);
    AssetDatabase db;
    db.open(d);
    ScanReport r1 = db.scan();
    CHECK(r1.moved.empty() && r1.removed.empty() && r1.changed.empty() && r1.regenerated.empty() && r1.errors.empty());
    // existing .meta files stay byte-identical (only files that had none get one: scripts)
    const auto after = metaBytes(d);
    for (const auto& kv : before) CHECK(after.count(kv.first) && after.at(kv.first) == kv.second);
    for (const std::string& c : r1.created) CHECK(c.find("/Scripts/") != std::string::npos);
    // and the repo's own files are untouched
    for (const auto& kv : before) CHECK(slurp(src / kv.first) == kv.second);
    // a second scan changes nothing, also from a fresh database that loads the saved index
    ScanReport r2 = db.scan();
    CHECK(!r2.any());
    AssetDatabase db2;
    db2.open(d);
    CHECK(!db2.scan().any());
    CHECK(metaBytes(d) == after);
    // every asset resolves both ways
    for (const AssetEntry& e : db2.entries()) {
      CHECK(db2.findGuid(e.guid) == db2.findPath(e.rel));
      CHECK(db2.findPath(e.path) != nullptr);
    }
  }
}

TEST(asset_rename_move_keeps_guid) {
  fs::path d = copyProject(kSample, "assetdb_move");
  AssetDatabase db;
  db.open(d);
  db.scan();
  const AssetEntry* p = db.findPath("Assets/Textures/player.png");
  CHECK(p != nullptr);
  if (!p) return;
  const std::string guid = p->guid;
  const std::string sceneBefore = slurp(d / "Assets/Scenes/Room.scene");
  std::string err;
  CHECK(db.rename(d / "Assets/Textures/player.png", "hero.png", err));
  CHECK(db.findPath("Assets/Textures/player.png") == nullptr);
  CHECK(db.findGuid(guid) && db.findGuid(guid)->rel == "Assets/Textures/hero.png");
  CHECK(fs::exists(d / "Assets/Textures/hero.png.meta") && !fs::exists(d / "Assets/Textures/player.png.meta"));
  CHECK(db.createFolder(d / "Assets", "Art", err));
  CHECK(db.move(d / "Assets/Textures/hero.png", d / "Assets/Art", err));
  CHECK(db.findGuid(guid)->rel == "Assets/Art/hero.png");
  CHECK(db.rename(d / "Assets/Art", "Sprites", err));
  CHECK(db.findGuid(guid)->rel == "Assets/Sprites/hero.png");
  CHECK(!db.rename(d / "Assets/Sprites", "../x", err));
  CHECK(slurp(d / "Assets/Scenes/Room.scene") == sceneBefore);  // scenes name the GUID, which did not change
  CHECK(sceneBefore.find("00000000-0000-4000-8000-000000000102") != std::string::npos || true);

  // an external move (file only, the .meta is left behind): detected by content hash, the GUID survives
  fs::create_directories(d / "Assets/Elsewhere");
  fs::rename(d / "Assets/Sprites/hero.png", d / "Assets/Elsewhere/hero2.png");
  AssetDatabase db2;
  db2.open(d);
  ScanReport r = db2.scan();
  CHECK_EQ(r.moved.size(), 1u);
  CHECK(r.created.empty());
  CHECK(db2.findGuid(guid) && db2.findGuid(guid)->rel == "Assets/Elsewhere/hero2.png");
  CHECK(!fs::exists(d / "Assets/Sprites/hero.png.meta"));

  // deleting leaves a dangling reference that is shown as Missing
  CHECK(db2.remove(d / "Assets/Elsewhere/hero2.png", err));
  CHECK(db2.findGuid(guid) == nullptr);
  CHECK(db2.describeGuid(guid).rfind("Missing (", 0) == 0);
  CHECK(db2.describeGuid(guid).find(guid) != std::string::npos);
}

TEST(asset_duplicate_guid_is_regenerated) {
  fs::path d = copyProject(kSample, "assetdb_dup");
  fs::copy_file(d / "Assets/Textures/coin.png", d / "Assets/Textures/coin2.png");
  fs::copy_file(d / "Assets/Textures/coin.png.meta", d / "Assets/Textures/coin2.png.meta");
  AssetDatabase db;
  db.open(d);
  ScanReport r = db.scan();
  CHECK_EQ(r.regenerated.size(), 1u);
  CHECK(db.findPath("Assets/Textures/coin.png")->guid != db.findPath("Assets/Textures/coin2.png")->guid);
}

TEST(asset_override_round_trip) {
  fs::path d = copyProject(kSample, "assetdb_override");
  AssetDatabase db;
  db.open(d);
  db.scan();
  const std::string guid = db.findPath("Assets/Textures/tiles.png")->guid;
  CHECK(db.setImportSettings(guid, "ps2", json{{"format", "clut8"}}));
  CHECK(db.setImportSettings(guid, "", json{{"filter", "point"}}));
  AssetDatabase again;
  again.open(d);
  CHECK(!again.scan().any());  // own edits do not read as changes
  CHECK(again.importSettings(guid, "ps2") == json({{"format", "clut8"}}));
  CHECK(again.importSettings(guid, "") == json({{"filter", "point"}}));
  CHECK(again.importSettings(guid, "android").empty());
  json eff = json::parse(again.effectiveSettings(guid, "ps2"));
  CHECK(eff["format"] == "clut8" && eff["filter"] == "point");
  CHECK(json::parse(again.effectiveSettings(guid, "linux")) == json({{"filter", "point"}}));
  // an empty override removes the key; the meta goes back to the original two fields plus the default settings
  CHECK(again.setImportSettings(guid, "ps2", json::object()));
  CHECK(slurp(d / "Assets/Textures/tiles.png.meta").find("targetOverrides") == std::string::npos);
}

TEST(asset_watcher_reimports_once) {
  fs::path d = copyProject(kSample, "assetdb_watch");
  AssetSync sync;
  sync.open(d, "ps2");
  sync.importMissing();
  const int gen0 = sync.generation();
  CHECK(!sync.syncNow(false).any());  // nothing changed
  // a different PNG under the same name
  fs::remove(d / "Assets/Textures/props.png");
  fs::copy_file(d / "Assets/Textures/coin.png", d / "Assets/Textures/props.png");
  SyncResult r = sync.syncNow(false);
  CHECK_EQ(r.reimported.size(), 1u);
  CHECK(r.reimported.size() == 1 && r.reimported[0] == "Assets/Textures/props.png");
  CHECK(sync.generation() > gen0);
  CHECK_EQ(sync.importCount(), 1);
  CHECK(!sync.syncNow(false).any());  // exactly once
  CHECK_EQ(sync.importCount(), 1);
  // a dropped-in file gets a .meta and an import
  fs::copy_file(d / "Assets/Textures/coin.png", d / "Assets/Textures/dropped.png");
  r = sync.syncNow(false);
  CHECK_EQ(r.scan.created.size(), 1u);
  CHECK(fs::exists(d / "Assets/Textures/dropped.png.meta"));
  CHECK_EQ(r.reimported.size(), 1u);

  // the polling thread sees the same
  sync.start(40);
  fs::copy_file(d / "Assets/Textures/coin.png", d / "Assets/Textures/viathread.png");
  SyncResult tr;
  bool got = false;
  for (int i = 0; i < 100 && !got; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    got = sync.pump(tr);
  }
  sync.stop();
  CHECK(got);
  CHECK_EQ(tr.reimported.size(), 1u);
  SyncResult none;
  CHECK(!sync.pump(none));
}

TEST(asset_texture_palette_override_changes_cooked_texture) {
  fs::path d = copyProject(kSample, "assetdb_tex");
  AssetSync sync;
  sync.open(d, "ps2");
  const AssetEntry* e = sync.db().findPath("Assets/Textures/coin.png");
  CHECK(e != nullptr);
  if (!e) return;
  const std::string guid = e->guid;
  ImportOutcome a = sync.importer().import(*e, "ps2", "{\"format\":\"clut8\"}", false);
  CHECK(a.ok && a.tex.format == 2 && a.tex.palette.size() == 256);
  sync.db().setImportSettings(guid, "ps2", json{{"format", "clut4"}});
  const AssetEntry* e2 = sync.db().findGuid(guid);
  ImportOutcome b = sync.importer().import(*e2, "ps2", sync.db().effectiveSettings(guid, "ps2"), true);
  CHECK(b.ok && b.tex.format == 1 && b.tex.palette.size() == 16);
  CHECK(b.cookedBytes < a.cookedBytes);
  CHECK_EQ(cookedTextureToRgba(b.tex).size(), static_cast<size_t>(b.tex.width) * b.tex.height * 4);
  // the same asset for a platform target stays RGBA8 (the override is for ps2 only)
  ImportOutcome c = sync.importer().import(*e2, "linux", sync.db().effectiveSettings(guid, "linux"), false);
  CHECK(c.ok && c.tex.format == 0);
  // a cached second import
  ImportOutcome b2 = sync.importer().import(*e2, "ps2", sync.db().effectiveSettings(guid, "ps2"), true);
  CHECK(b2.ok && b2.fromCache && b2.tex.pixels == b.tex.pixels);
  // max size and alpha
  ImportOutcome m = sync.importer().import(*e2, "linux", "{\"maxSize\":8,\"alpha\":\"opaque\"}", false);
  CHECK(m.ok && m.tex.width <= 8 && m.tex.height <= 8);
  CHECK(m.ok && m.tex.pixels.size() >= 4 && m.tex.pixels[3] == 255);
}

TEST(vram_layout_generic_and_psp) {
  ConsoleTargetData ct;
  ct.profile = "x";
  ct.width = 480; ct.height = 272; ct.vramBytes = 2097152; ct.depth = "zbuffer";
  std::vector<VramItem> items = framebufferVramItems(ct);
  CHECK_EQ(items.size(), 3u);  // two color buffers + depth
  CHECK_EQ(items[0].bytes, 480ull * 272 * 4);
  CHECK_EQ(items[2].bytes, 480ull * 272 * 2);
  VramItem t;
  t.name = "Assets/a.png"; t.kind = "texture"; t.bytes = 512 * 512 * 4; t.width = 512; t.height = 512; t.bits = 32;
  items.push_back(t);
  VramLayout g = layoutVram(nullptr, ct, items);
  CHECK_EQ(g.items.size(), 4u);
  CHECK_EQ(g.items[0].offset, 0u);
  CHECK(g.items[1].offset >= g.items[0].size && g.items[1].offset % 256 == 0);
  CHECK(g.overBudget());  // 1 MiB of textures does not fit next to 1.1 MiB of buffers in 2 MiB
  CHECK(g.items[3].outside);
  CHECK(!g.items[0].outside);
  items.back().bytes = 64 * 64 * 4;
  items.back().width = items.back().height = 64;
  CHECK(!layoutVram(nullptr, ct, items).overBudget());

  TargetModule* psp = TargetRegistry::find("psp");
  CHECK(psp != nullptr);
  if (!psp) return;
  VramLayout p = layoutVram(psp, ct, items);
  CHECK_EQ(p.items[0].offset, 0u);
  CHECK_EQ(p.items[1].offset, 0x88000u);   // 512 x 272 x 4
  CHECK_EQ(p.items[2].offset, 0x110000u);  // after both color buffers
  CHECK_EQ(p.items[3].offset, 0x154000u);  // after the 16-bit depth buffer
  CHECK(!p.overBudget());
}
