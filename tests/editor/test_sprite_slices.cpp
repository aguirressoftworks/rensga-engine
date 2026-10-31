// F4.08 part 1 (Editor 7): sprite slices in the .meta "sprites" key. Host only.
#include <filesystem>
#include <fstream>
#include <iterator>

#include "rensga/Rensga.h"
#include "rensga/editor/asset_database.h"
#include "rensga/editor/sprite_slices.h"
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

}  // namespace

TEST(slices_grid_generation) {
  SpriteSlices s;
  s.cellW = 16; s.cellH = 16;
  CHECK_EQ(s.generateGrid(64, 32, "player"), 8);
  CHECK(s.mode == SliceMode::Grid);
  CHECK(s.rects[0] == (SpriteRect{"player_0", 0, 0, 16, 16, 0, 0}));
  CHECK(s.rects[3] == (SpriteRect{"player_3", 48, 0, 16, 16, 0, 0}));
  CHECK(s.rects[4] == (SpriteRect{"player_4", 0, 16, 16, 16, 0, 0}));
  CHECK(s.rects[7] == (SpriteRect{"player_7", 48, 16, 16, 16, 0, 0}));
  std::string err;
  CHECK(s.validate(64, 32, err));
  // padding and offset; only whole cells count
  SpriteSlices p;
  p.cellW = 10; p.cellH = 8; p.paddingX = 2; p.paddingY = 1; p.offsetX = 3; p.offsetY = 4;
  CHECK_EQ(SpriteSlices::gridColumns(40, 10, 2, 3), 3);  // x = 3, 15, 27 (27 + 10 = 37 <= 40)
  CHECK_EQ(SpriteSlices::gridRows(30, 8, 1, 4), 3);      // y = 4, 13, 22 (22 + 8 = 30 <= 30)
  CHECK_EQ(p.generateGrid(40, 30, "t"), 9);
  CHECK(p.rects[1] == (SpriteRect{"t_1", 15, 4, 10, 8, 0, 0}));
  CHECK(p.rects[8] == (SpriteRect{"t_8", 27, 22, 10, 8, 0, 0}));
  CHECK(p.validate(40, 30, err));
  CHECK(!p.validate(36, 30, err));   // the last column no longer fits
  CHECK(err.find("t_2") != std::string::npos);
  // a texture smaller than a cell, and a zero cell size, give nothing
  SpriteSlices q;
  q.cellW = 16; q.cellH = 16;
  CHECK_EQ(q.generateGrid(8, 8, "x"), 0);
  SpriteSlices z;
  CHECK_EQ(z.generateGrid(64, 64, "x"), 0);
}

TEST(slices_regenerate_keeps_names_and_pivots) {
  SpriteSlices s;
  s.cellW = 16; s.cellH = 16;
  s.generateGrid(64, 16, "a");   // 4 cells
  CHECK(s.renameRect(1, "jump"));
  CHECK(s.setPivot(1, PivotPreset::Bottom));
  CHECK_EQ(s.rects[1].pivotX, 8);
  CHECK_EQ(s.rects[1].pivotY, 16);
  s.cellW = 8;                    // finer grid: 8 cells
  CHECK_EQ(s.generateGrid(64, 16, "a"), 8);
  CHECK(s.rects[1].name == "jump");
  CHECK_EQ(s.rects[1].pivotX, 8);
  CHECK(s.rects[1].w == 8 && s.rects[1].x == 8);
  CHECK(s.rects[4].name == "a_4");
  // a kept name never collides with a generated one
  SpriteSlices u;
  u.cellW = 8; u.cellH = 8;
  u.rects.push_back({"n_1", 0, 0, 8, 8, 0, 0});
  u.rects.push_back({"", 0, 0, 8, 8, 0, 0});
  u.generateGrid(32, 8, "n");
  std::string err;
  CHECK(u.validate(32, 8, err));  // all names unique
  CHECK(u.rects[0].name == "n_1");
  CHECK(u.rects[1].name != "n_1");
  // keepExisting = false starts over
  u.generateGrid(32, 8, "n", false);
  CHECK(u.rects[0].name == "n_0");
}

TEST(slices_manual_rect_editing) {
  SpriteSlices s;
  CHECK_EQ(s.addRect("idle", 0, 0, 16, 24), 0);
  CHECK_EQ(s.addRect("run", 16, 0, 16, 24), 1);
  CHECK_EQ(s.addRect("", 32, 0, 8, 8), 2);       // empty name -> sprite_0
  CHECK(s.rects[2].name == "sprite_0");
  CHECK_EQ(s.addRect("idle", 40, 0, 8, 8), 3);   // used name -> a unique one
  CHECK(s.rects[3].name == "sprite_1");
  CHECK_EQ(s.addRect("bad", 0, 0, 0, 5), -1);
  CHECK_EQ(s.find("run"), 1);
  CHECK_EQ(s.find("nope"), -1);
  CHECK(!s.renameRect(1, "idle"));
  CHECK(!s.renameRect(1, ""));
  CHECK(s.renameRect(1, "run"));                  // own name is fine
  CHECK(s.renameRect(1, "sprint"));
  CHECK(s.setRect(1, 16, 1, 17, 23));
  CHECK(!s.setRect(1, 0, 0, 0, 1));
  CHECK(!s.setRect(9, 0, 0, 1, 1));
  CHECK(s.setPivot(0, PivotPreset::BottomRight));
  CHECK(s.rects[0].pivotX == 16 && s.rects[0].pivotY == 24);
  CHECK(s.setPivot(0, PivotPreset::Center));
  CHECK(s.rects[0].pivotX == 8 && s.rects[0].pivotY == 12);
  CHECK(s.setPivot(0, PivotPreset::TopLeft));
  CHECK(s.rects[0].pivotX == 0 && s.rects[0].pivotY == 0);
  CHECK(s.setPivot(0, 3, -2));
  CHECK_EQ(s.indexAt(2, 2), 0);
  CHECK_EQ(s.indexAt(20, 10), 1);
  CHECK_EQ(s.indexAt(100, 100), -1);
  CHECK_EQ(s.indexOfSrc(16, 1, 17, 23), 1);
  CHECK_EQ(s.indexOfSrc(0, 0, 1, 1), -1);
  s.setRect(1, 8, 0, 16, 24);                     // overlaps slice 0: the later one is on top
  CHECK_EQ(s.indexAt(10, 5), 1);
  CHECK(s.removeRect(0));
  CHECK(!s.removeRect(9));
  CHECK_EQ(s.rects.size(), 3u);
  std::string err;
  CHECK(!s.validate(16, 24, err));                // sprint reaches x = 24
  CHECK(s.validate(64, 64, err));
  SpriteSlices dup;
  dup.rects.push_back({"a", 0, 0, 1, 1, 0, 0});
  dup.rects.push_back({"a", 1, 0, 1, 1, 0, 0});
  CHECK(!dup.validate(8, 8, err));
}

TEST(slices_json_round_trip) {
  SpriteSlices g;
  g.cellW = 16; g.cellH = 12; g.paddingX = 1; g.paddingY = 1; g.offsetX = 2; g.offsetY = 3;
  g.generateGrid(70, 60, "hero");
  g.setPivot(2, PivotPreset::Bottom);
  g.renameRect(2, "land");
  const json j = g.toJson();
  CHECK(j["mode"] == "grid");
  CHECK(j["padding"] == 1);                       // equal padding is one number
  CHECK((j["offset"] == json::array({2, 3})));
  CHECK(j["rects"].size() == g.rects.size());
  SpriteSlices back;
  std::string err;
  CHECK(SpriteSlices::fromJson(j, back, err));
  CHECK(back == g);
  g.paddingY = 2;
  CHECK((g.toJson()["padding"] == json::array({1, 2})));
  SpriteSlices m;
  m.addRect("a", 1, 2, 3, 4);
  CHECK(m.toJson()["mode"] == "manual");
  CHECK(!m.toJson().contains("cellW"));
  SpriteSlices mb;
  CHECK(SpriteSlices::fromJson(m.toJson(), mb, err));
  CHECK(mb == m);
  // pivots are optional on read
  CHECK(SpriteSlices::fromJson(json::parse(R"({"mode":"manual","rects":[{"name":"x","x":0,"y":0,"w":4,"h":4}]})"), mb, err));
  CHECK(mb.rects[0].pivotX == 0);
  // bad values
  CHECK(!SpriteSlices::fromJson(json::parse(R"({"mode":"strip"})"), mb, err));
  CHECK(!SpriteSlices::fromJson(json::parse(R"({"mode":"grid","cellW":0,"cellH":8})"), mb, err));
  CHECK(!SpriteSlices::fromJson(json::parse(R"({"mode":"grid","cellW":8})"), mb, err));
  CHECK(!SpriteSlices::fromJson(json::parse(R"({"mode":"manual","rects":[{"name":"","x":0,"y":0,"w":1,"h":1}]})"), mb, err));
  CHECK(!SpriteSlices::fromJson(json::parse(R"({"mode":"manual","rects":[{"name":"a","x":0,"y":0,"w":0,"h":1}]})"), mb, err));
  CHECK(!SpriteSlices::fromJson(json::parse("[1]"), mb, err));
  CHECK(!err.empty());
  CHECK(mb.rects.size() == 1);  // untouched by the failures
}

TEST(slices_apply_to_sprite_renderer) {
  SpriteSlices s;
  s.addRect("idle", 32, 16, 20, 24);
  s.setPivot(0, 10, 24);
  SliceFields f = sliceFields(s.rects[0]);
  CHECK(f.srcX == 32 && f.srcY == 16 && f.srcW == 20 && f.srcH == 24 && f.pivotX == 10 && f.pivotY == 24);
  SpriteRenderer sr;
  sr.sortingOrder = 7;
  sr.flipX = true;
  CHECK(applySlice(sr, s, "idle"));
  CHECK(sr.srcX == 32 && sr.srcY == 16 && sr.srcW == 20 && sr.srcH == 24);
  CHECK(sr.pivotX == 10 && sr.pivotY == 24);
  CHECK(sr.sortingOrder == 7 && sr.flipX);       // other fields untouched
  CHECK(!applySlice(sr, s, "missing"));
  CHECK(sr.srcW == 20);
  s.addRect("run", 0, 0, 8, 8);
  applySlice(sr, s.rects[1]);
  CHECK(sr.srcW == 8 && sr.pivotX == 0);
}

TEST(slices_meta_read_write_keeps_import_settings) {
  const fs::path src = fs::path(RENSGA_SAMPLE_DIR);
  const fs::path d = copyProject(src, "slices_meta");
  AssetDatabase db;
  db.open(d);
  db.scan();
  const AssetEntry* tex = db.findPath("Assets/Textures/player.png");
  CHECK(tex != nullptr);
  if (!tex) return;
  const std::string guid = tex->guid;
  const std::string metaPath = (d / "Assets/Textures/player.png.meta").string();
  const json before = json::parse(slurp(metaPath));
  SpriteSlices got;
  std::string err;
  CHECK(loadSlices(db, guid, got, err));
  CHECK(got.empty());                              // no key yet: empty slices, success
  CHECK(db.sprites(guid).is_null());
  SpriteSlices s;
  s.cellW = 24; s.cellH = 32;
  s.generateGrid(96, 64, "player");
  s.renameRect(0, "stand");
  s.setPivot(0, PivotPreset::Bottom);
  CHECK(saveSlices(db, guid, s, err));
  // on disk: a new top-level key; everything else is as it was
  json after = json::parse(slurp(metaPath));
  CHECK(after.contains("sprites"));
  CHECK(after["sprites"]["rects"].size() == 8);
  json rest = after;
  rest.erase("sprites");
  CHECK(rest == before);
  CHECK(after["importSettings"] == before["importSettings"]);
  // a fresh database reads it back, and scanning does not call it a change
  AssetDatabase db2;
  db2.open(d);
  CHECK(!db2.scan().any());
  SpriteSlices again;
  CHECK(loadSlices(db2, guid, again, err));
  CHECK(again == s);
  // import settings edits keep the slices, and the other way round
  json st = db2.importSettings(guid, "");
  st["maxSize"] = 128;
  CHECK(db2.setImportSettings(guid, "", st));
  CHECK(json::parse(slurp(metaPath)).contains("sprites"));
  s.addRect("extra", 0, 0, 4, 4);
  CHECK(saveSlices(db2, guid, s, err));
  CHECK(db2.importSettings(guid, "")["maxSize"] == 128);
  // empty slices remove the key again
  CHECK(saveSlices(db2, guid, SpriteSlices(), err));
  CHECK(!json::parse(slurp(metaPath)).contains("sprites"));
  // unknown GUID
  CHECK(!saveSlices(db2, "ffffffff-ffff-4fff-8fff-ffffffffffff", s, err));
  CHECK(!err.empty());
  CHECK(!db2.setSprites("ffffffff-ffff-4fff-8fff-ffffffffffff", json::object({{"mode", "manual"}})));
  // a malformed value reports instead of loading
  CHECK(db2.setSprites(guid, json::object({{"mode", "strip"}})));
  CHECK(!loadSlices(db2, guid, again, err));
  CHECK(!db2.setSprites(guid, json::array()));
}
