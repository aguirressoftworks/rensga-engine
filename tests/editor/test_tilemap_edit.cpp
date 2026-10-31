// F4.08 part 1 (Editor 7): TilemapEdit (load/save, strokes, fill, rule tiles, tile flags, resize, undo) and RuleTile. Host only.
#include <filesystem>
#include <fstream>
#include <iterator>

#include "rensga/editor/scene2d.h"
#include "rensga/editor/tilemap_edit.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path tmpDir(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}

const char* kGuid = "00000000-0000-4000-8000-000000000101";

TilemapEdit* fresh(TilemapEdit& m, int w = 8, int h = 6) {
  m.create(kGuid, 16, w, h);
  return &m;
}

// 4-neighbour autotile: the tile id is the neighbour mask (0..15), default 0.
RuleTile autotile4(bool outsideFilled) {
  RuleTile r;
  r.neighbours = 4;
  r.outsideFilled = outsideFilled;
  r.defaultTile = 0;
  for (int m = 0; m < 16; ++m) r.rules.push_back({static_cast<uint8_t>(m), 0x0F, static_cast<uint16_t>(m)});
  return r;
}

}  // namespace

TEST(tilemap_sample_round_trip_is_byte_identical) {
  const fs::path src = fs::path(RENSGA_SAMPLE_DIR) / "Assets/Tiles/room.tilemap.json";
  const std::string bytes = slurp(src);
  CHECK(!bytes.empty());
  TilemapEdit m;
  std::string err;
  CHECK(m.load(src, err));
  CHECK(err.empty());
  CHECK(!m.dirty());
  CHECK_EQ(m.width(), 40);
  CHECK_EQ(m.height(), 14);
  CHECK_EQ(m.tileSize(), 32);
  CHECK(m.tileset() == "00000000-0000-4000-8000-000000000101");
  CHECK(m.warnings().empty());
  CHECK(m.toText() == bytes);
  CHECK(m.serialize() == bytes);  // the canonical formatting is the one the generator writes
  CHECK(m.tileProps(0).kind == TileKind::Solid);
  CHECK(m.tileProps(3).kind == TileKind::Solid);
  CHECK(m.tileProps(6).kind == TileKind::OneWay);
  CHECK(m.tileProps(4).kind == TileKind::Slope && m.tileProps(4).slope == "45R");
  CHECK(m.tileProps(5).kind == TileKind::Slope && m.tileProps(5).slope == "45L");
  CHECK(m.tileProps(2).kind == TileKind::None);
  const fs::path out = tmpDir("tilemap_roundtrip") / "room.tilemap.json";
  CHECK(m.saveAs(out, err));
  CHECK(slurp(out) == bytes);
  CHECK(!m.dirty());
  // an edit and its undo return to the very same bytes
  const uint16_t was = m.at(3, 3);
  CHECK(m.paint(3, 3, 9) == 1);
  CHECK(m.dirty());
  CHECK(m.serialize() != bytes);
  CHECK(m.undo());
  CHECK_EQ(m.at(3, 3), was);
  CHECK(!m.dirty());
  CHECK(m.toText() == bytes);
}

TEST(tilemap_keeps_unknown_keys_and_order) {
  const std::string text =
      "{\n \"format\": 1,\n \"editorNote\": {\n  \"a\": [\n   1,\n   2\n  ]\n },\n \"tileset\": \"g\",\n \"tileSize\": 8,\n \"width\": 2,\n"
      " \"height\": 2,\n \"tiles\": [\n  1,\n  2,\n  3,\n  4\n ],\n \"layers\": 3\n}\n";
  TilemapEdit m;
  std::string err;
  CHECK(m.parse(text, err));
  CHECK(m.serialize() == text);
  m.paint(0, 0, 7);
  const json j = json::parse(m.serialize());
  CHECK(j["editorNote"]["a"].size() == 2);
  CHECK_EQ(j["layers"].get<int>(), 3);
  CHECK_EQ(j["tiles"][0].get<int>(), 7);
  const std::string out = m.serialize();
  CHECK(out.find("editorNote") < out.find("tileset"));  // positions kept
  CHECK(out.find("layers") > out.find("tiles"));
  CHECK(out.find("\"solid\"") == std::string::npos);    // no flag keys appear from nowhere
}

TEST(tilemap_load_rejects_bad_files) {
  TilemapEdit m;
  std::string err;
  CHECK(!m.parse("not json", err));
  CHECK(!m.parse("{\"format\":2}", err));
  CHECK(!m.parse("{\"format\":1,\"tileSize\":16,\"width\":2,\"height\":2,\"tiles\":[1,2,3,70000]}", err));
  CHECK(!m.parse("{\"format\":1,\"tileSize\":16,\"width\":2,\"height\":2,\"tiles\":[0,0,0,0],\"slopes\":{\"1\":\"60L\"}}", err));
  CHECK(!err.empty());
  CHECK(!m.parse("{\"format\":1,\"tileSize\":0,\"width\":2,\"height\":2,\"tiles\":[0,0,0,0]}", err));
  // a short tiles array and an id with two flags load with warnings
  CHECK(m.parse("{\"format\":1,\"tileSize\":16,\"width\":2,\"height\":2,\"tiles\":[0,0,0],\"solid\":[1],\"oneWay\":[1]}", err));
  CHECK_EQ(m.warnings().size(), 2u);
  CHECK_EQ(m.tiles().size(), 4u);
  CHECK_EQ(m.at(1, 1), kTileEmpty);
  CHECK(m.tileProps(1).kind == TileKind::Solid);
  CHECK(!m.dirty());
}

TEST(tilemap_paint_erase_rect_and_bounds) {
  TilemapEdit m;
  fresh(m);
  CHECK(m.dirty());  // never saved
  CHECK_EQ(m.paint(2, 2, 5), 1);
  CHECK_EQ(m.at(2, 2), 5);
  CHECK_EQ(m.paint(2, 2, 5), 0);   // same value: nothing written, no Command
  CHECK_EQ(m.undoStack().depth(), 1u);
  CHECK_EQ(m.paint(-1, 0, 5), 0);  // outside the map
  CHECK_EQ(m.paint(8, 0, 5), 0);
  CHECK_EQ(m.at(-1, 0), kTileEmpty);
  CHECK_EQ(m.paintRect(1, 1, 3, 2, 4), 6);  // 3x2 = 6 cells, one of them held 5
  CHECK_EQ(m.at(1, 1), 4);
  CHECK_EQ(m.at(3, 2), 4);
  CHECK_EQ(m.paintRect(6, 4, 20, 20, 9), 4);  // clipped to the map: cols 6..7, rows 4..5
  CHECK_EQ(m.paintRect(5, 5, 4, 4, 1), 4);     // corners in any order
  CHECK_EQ(m.eraseRect(0, 0, 7, 5), 6 + 4 + 4);  // only the painted cells count
  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 8; ++x) CHECK_EQ(m.at(x, y), kTileEmpty);
  CHECK_EQ(m.undoStack().depth(), 5u);  // paint, rect, rect, rect, erase: one Command each
}

TEST(tilemap_stroke_is_one_undo_command) {
  TilemapEdit m;
  fresh(m);
  const std::vector<uint16_t> blank = m.tiles();
  m.beginStroke("Brush");
  CHECK(m.inStroke());
  for (int x = 0; x < 6; ++x) m.paint(x, 1, 3);
  m.paint(2, 1, 6);  // a cell touched twice
  m.erase(0, 1);
  CHECK_EQ(m.undoStack().depth(), 0u);  // nothing recorded until the stroke ends
  CHECK_EQ(m.at(2, 1), 6);              // but the map shows the edits
  CHECK(m.endStroke());
  CHECK(!m.inStroke());
  CHECK_EQ(m.undoStack().depth(), 1u);
  CHECK(std::string(m.undoStack().undoName()) == "Brush");
  const std::vector<uint16_t> painted = m.tiles();
  CHECK(painted != blank);
  CHECK_EQ(m.at(0, 1), kTileEmpty);
  CHECK(m.undo());
  CHECK(m.tiles() == blank);
  CHECK(m.redo());
  CHECK(m.tiles() == painted);
  CHECK(m.undo());
  CHECK(m.tiles() == blank);
  CHECK(!m.canUndo());
  // a stroke that ends where it began records nothing
  m.beginStroke();
  m.paint(4, 4, 1);
  m.paint(4, 4, kTileEmpty);
  CHECK(!m.endStroke());
  CHECK_EQ(m.undoStack().depth(), 0u);
  CHECK_EQ(m.undoStack().redoDepth(), 1u);  // the empty stroke did not drop the redo branch
  // undo() closes an open stroke first
  m.beginStroke();
  m.paint(1, 1, 2);
  CHECK(m.undo());  // the stroke became a command, then it was undone
  CHECK_EQ(m.at(1, 1), kTileEmpty);
  CHECK(m.redo());
  CHECK_EQ(m.at(1, 1), 2);
}

TEST(tilemap_flood_fill_and_undo) {
  TilemapEdit m;
  fresh(m, 6, 5);
  // a wall of tile 1 splits the map into a left and a right part
  m.beginStroke();
  for (int y = 0; y < 5; ++y) m.paint(3, y, 1);
  m.endStroke();
  const std::vector<uint16_t> walled = m.tiles();
  CHECK_EQ(m.fill(0, 0, 8), 3 * 5);  // left part only
  CHECK_EQ(m.at(2, 4), 8);
  CHECK_EQ(m.at(4, 0), kTileEmpty);
  CHECK_EQ(m.at(3, 2), 1);
  CHECK_EQ(m.undoStack().depth(), 2u);  // the fill is one Command
  CHECK_EQ(m.fill(0, 0, 8), 0);         // already that tile
  CHECK_EQ(m.fill(-1, 0, 8), 0);
  CHECK_EQ(m.fill(4, 0, 8), 2 * 5);
  CHECK(m.undo());
  CHECK(m.undo());
  CHECK(m.tiles() == walled);
  CHECK(m.redo());
  CHECK(m.redo());
  CHECK_EQ(m.at(5, 4), 8);
  CHECK_EQ(m.fill(3, 2, kTileEmpty), 5);  // fill with "empty" erases the wall
  CHECK_EQ(m.at(3, 2), kTileEmpty);
  // fill is 4-connected: a diagonal gap does not leak
  TilemapEdit d;
  fresh(d, 3, 3);
  d.paint(1, 0, 1); d.paint(0, 1, 1); d.paint(2, 1, 1); d.paint(1, 2, 1);  // a diamond around (1,1)
  CHECK_EQ(d.fill(1, 1, 7), 1);
  CHECK_EQ(d.at(0, 0), kTileEmpty);
}

TEST(tilemap_dirty_tracking_and_save) {
  TilemapEdit m;
  fresh(m);
  const fs::path file = tmpDir("tilemap_dirty") / "a.tilemap.json";
  std::string err;
  CHECK(!m.save(err));  // no path yet
  CHECK(m.saveAs(file, err));
  CHECK(!m.dirty());
  CHECK(m.path() == file);
  CHECK(slurp(file) == m.serialize());
  m.paint(1, 1, 2);
  CHECK(m.dirty());
  m.undo();
  CHECK(!m.dirty());
  m.redo();
  CHECK(m.dirty());
  CHECK(m.save(err));
  CHECK(!m.dirty());
  CHECK(slurp(file) == m.toText());
  m.undo();               // back before the saved state
  CHECK(m.dirty());
  m.paint(5, 5, 3);       // a new branch: the saved state can never come back
  CHECK(m.dirty());
  m.undo();
  CHECK(m.dirty());
  // reload gives the same model
  TilemapEdit r;
  CHECK(r.load(file, err));
  CHECK(!r.dirty());
  CHECK_EQ(r.at(1, 1), 2);
  CHECK_EQ(r.at(5, 5), kTileEmpty);
  CHECK(r.toText() == slurp(file));
  // the editor's own reader and the cooker's field names agree with what was written
  TilemapData td;
  CHECK(readTilemap(file, td));
  CHECK_EQ(td.width, 8);
  CHECK_EQ(td.tiles[1 * 8 + 1], 2);
  CHECK_EQ(td.tiles[0], 65535);
  // settings changes save too
  m.setTileset("11111111-1111-4111-8111-111111111111", 32);
  CHECK(m.dirty());
  CHECK(m.save(err));
  TilemapEdit r2;
  CHECK(r2.load(file, err));
  CHECK_EQ(r2.tileSize(), 32);
  CHECK(r2.tileset() == "11111111-1111-4111-8111-111111111111");
  m.undo();
  CHECK_EQ(m.tileSize(), 16);
  CHECK(m.tileset() == kGuid);
}

TEST(tilemap_tile_flags_one_per_id) {
  TilemapEdit m;
  fresh(m);
  CHECK(m.setTileProps(3, {TileKind::Solid, ""}));
  CHECK(m.setTileProps(5, {TileKind::OneWay, ""}));
  CHECK(m.setTileProps(2, {TileKind::Slope, "22L_hi"}));
  CHECK(!m.setTileProps(4, {TileKind::Slope, "60L"}));  // unknown slope
  CHECK(!m.setTileProps(4, {TileKind::Slope, ""}));
  CHECK(!m.setTileProps(kTileEmpty, {TileKind::Solid, ""}));
  CHECK_EQ(m.undoStack().depth(), 3u);
  CHECK(m.setTileProps(3, {TileKind::Solid, ""}));  // unchanged: no Command
  CHECK_EQ(m.undoStack().depth(), 3u);
  // changing the kind replaces the old one: the id is in exactly one list
  CHECK(m.setTileProps(3, {TileKind::OneWay, ""}));
  CHECK(m.tileProps(3).kind == TileKind::OneWay);
  json j = json::parse(m.serialize());
  CHECK(!j.contains("solid"));
  CHECK((j["oneWay"] == json::array({3, 5})));  // sorted ascending
  CHECK(j["slopes"]["2"] == "22L_hi");
  CHECK(m.setTileProps(3, {TileKind::Solid, "ignored"}));
  CHECK(m.tileProps(3).slope.empty());
  CHECK(m.setTileProps(3, {}));  // clear
  CHECK(m.tileProps(3).kind == TileKind::None);
  CHECK_EQ(m.flaggedTiles().size(), 2u);
  // undo/redo restore flags exactly
  for (int i = 0; i < 3; ++i) CHECK(m.undo());  // undoes: clear, ->Solid, ->OneWay
  CHECK(m.tileProps(3).kind == TileKind::Solid && m.tileProps(5).kind == TileKind::OneWay);
  while (m.undo()) {}
  CHECK(m.flaggedTiles().empty());
  while (m.redo()) {}
  CHECK(m.tileProps(2).slope == "22L_hi");
  CHECK(m.tileProps(3).kind == TileKind::None);
  // the cooker's rule holds for what we write
  std::string text = m.serialize(), err;
  TilemapEdit r;
  CHECK(r.parse(text, err));
  CHECK(r.warnings().empty());
  CHECK(r.flaggedTiles() == m.flaggedTiles());
}

TEST(tilemap_resize_keeps_content_and_undoes) {
  TilemapEdit m;
  fresh(m, 4, 3);
  m.paint(0, 0, 1);
  m.paint(3, 2, 2);
  const std::vector<uint16_t> before = m.tiles();
  CHECK(m.resize(6, 5));
  CHECK_EQ(m.width(), 6);
  CHECK_EQ(m.height(), 5);
  CHECK_EQ(m.at(0, 0), 1);
  CHECK_EQ(m.at(3, 2), 2);
  CHECK_EQ(m.at(5, 4), kTileEmpty);
  CHECK(m.resize(4, 4, 1, 1));  // shifts the content by one cell, cuts what falls outside
  CHECK_EQ(m.at(1, 1), 1);
  CHECK_EQ(m.at(0, 0), kTileEmpty);
  CHECK_EQ(m.at(3, 3), kTileEmpty);  // (3,2)+1 = (4,3) is cut
  CHECK(!m.resize(0, 4));
  CHECK(!m.resize(4, TilemapEdit::kMaxSide + 1));
  CHECK_EQ(m.undoStack().depth(), 4u);  // two paints, two resizes
  CHECK(m.undo());
  CHECK(m.undo());
  CHECK_EQ(m.width(), 4);
  CHECK(m.tiles() == before);
  CHECK(m.redo());
  CHECK_EQ(m.width(), 6);
  // painting inside a stroke, then a resize, keeps both as separate Commands
  m.beginStroke();
  m.paint(5, 4, 8);
  CHECK(m.resize(3, 3));
  CHECK(!m.inStroke());
  CHECK_EQ(m.undoStack().depth(), 5u);  // the stroke and the resize are two Commands
  CHECK(m.undo());
  CHECK_EQ(m.width(), 6);
  CHECK_EQ(m.at(5, 4), 8);
  CHECK(m.undo());
  CHECK_EQ(m.at(5, 4), kTileEmpty);
}

TEST(tilemap_shared_undo_stack) {
  UndoStack shared;
  TilemapEdit m;
  fresh(m);
  m.setUndoStack(&shared);
  m.paint(1, 1, 3);
  m.paintRect(2, 2, 4, 4, 5);
  CHECK_EQ(shared.depth(), 2u);
  CHECK_EQ(m.undoStack().depth(), 2u);
  CHECK(shared.undo());  // the scene's own Ctrl+Z reaches the map
  CHECK_EQ(m.at(3, 3), kTileEmpty);
  CHECK(shared.redo());
  CHECK_EQ(m.at(3, 3), 5);
  CHECK(m.undo());
  CHECK_EQ(m.at(3, 3), kTileEmpty);
  CHECK(m.dirty());
}

TEST(ruletile_parse_toText_and_resolve) {
  const std::string text =
      "{\"format\":1,\"tileset\":\"abc\",\"neighbours\":8,\"outside\":\"empty\",\"default\":7,\"members\":[30,31],"
      "\"rules\":[{\"mask\":255,\"tile\":5},{\"mask\":15,\"care\":15,\"tile\":6},{\"mask\":3,\"care\":3,\"tile\":9}]}";
  RuleTile r;
  std::string err;
  CHECK(r.parse(text, err));
  CHECK_EQ(r.neighbours, 8);
  CHECK(!r.outsideFilled);
  CHECK_EQ(r.defaultTile, 7);
  CHECK_EQ(r.rules.size(), 3u);
  CHECK_EQ(r.rules[0].care, 0xFF);  // default care for 8 neighbours
  CHECK_EQ(r.resolve(0xFF), 5);
  CHECK_EQ(r.resolve(0x0F), 6);     // corners missing, edges all there
  CHECK_EQ(r.resolve(0x0F | RuleBit::NE), 6);
  CHECK_EQ(r.resolve(RuleBit::N | RuleBit::E), 9);  // first matching rule wins; care = N and E only
  CHECK_EQ(r.resolve(RuleBit::S), 7);
  CHECK(r.isMember(5) && r.isMember(7) && r.isMember(30));
  CHECK(!r.isMember(8) && !r.isMember(kTileEmpty));
  CHECK(r.isRuleTile(9) && !r.isRuleTile(30));
  RuleTile r2;
  CHECK(r2.parse(r.toText(), err));
  CHECK(r2.toText() == r.toText());
  CHECK(r2.rules.size() == 3 && r2.members == r.members && r2.tileset == "abc");
  // bad files
  CHECK(!r2.parse("{\"format\":1}", err));  // no default
  CHECK(!r2.parse("{\"format\":1,\"default\":0,\"neighbours\":6}", err));
  CHECK(!r2.parse("{\"format\":1,\"default\":0,\"rules\":[{\"mask\":300,\"tile\":1}]}", err));
  CHECK(!r2.parse("{\"format\":1,\"default\":0,\"outside\":\"maybe\"}", err));
  CHECK(!r2.parse("{\"format\":2,\"default\":0}", err));
  // a failed parse leaves the object as it was
  CHECK_EQ(r2.rules.size(), 3u);
}

TEST(ruletile_four_neighbour_block_and_edges) {
  RuleTile r = autotile4(false);
  TilemapEdit m;
  fresh(m, 6, 6);
  CHECK(m.paintRuleRect(1, 1, 3, 3, r) > 0);
  CHECK_EQ(m.undoStack().depth(), 1u);
  CHECK_EQ(m.at(2, 2), 15);                             // fully surrounded
  CHECK_EQ(m.at(1, 1), RuleBit::E | RuleBit::S);        // top-left corner
  CHECK_EQ(m.at(3, 1), RuleBit::W | RuleBit::S);
  CHECK_EQ(m.at(1, 3), RuleBit::N | RuleBit::E);
  CHECK_EQ(m.at(3, 3), RuleBit::N | RuleBit::W);
  CHECK_EQ(m.at(2, 1), RuleBit::E | RuleBit::S | RuleBit::W);  // top edge
  CHECK_EQ(m.at(2, 0), kTileEmpty);
  // erasing the middle cell re-picks the ring around it
  CHECK(m.eraseRule(2, 2, r) > 0);
  CHECK_EQ(m.at(2, 2), kTileEmpty);
  CHECK_EQ(m.at(2, 1), RuleBit::E | RuleBit::W);        // lost its south neighbour
  CHECK_EQ(m.at(1, 2), RuleBit::N | RuleBit::S);
  CHECK_EQ(m.at(1, 1), RuleBit::E | RuleBit::S);        // not adjacent to (2,2): unchanged
  CHECK_EQ(m.undoStack().depth(), 2u);
  CHECK(m.undo());
  CHECK_EQ(m.at(2, 2), 15);
  CHECK_EQ(m.at(2, 1), RuleBit::E | RuleBit::S | RuleBit::W);
  CHECK(m.undo());
  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 6; ++x) CHECK_EQ(m.at(x, y), kTileEmpty);
  CHECK(m.redo());
  CHECK_EQ(m.at(2, 2), 15);
  // a single cell, far from everything, is the lone tile (mask 0)
  CHECK_EQ(m.paintRule(5, 5, r), 1);
  CHECK_EQ(m.at(5, 5), 0);
}

TEST(ruletile_map_border_inside_and_outside) {
  TilemapEdit a, b;
  fresh(a, 4, 4);
  fresh(b, 4, 4);
  const RuleTile filled = autotile4(true), empty = autotile4(false);
  a.paintRule(0, 0, filled);  // the border counts as ground: N and W are set, E and S are not
  CHECK_EQ(a.at(0, 0), RuleBit::N | RuleBit::W);
  b.paintRule(0, 0, empty);
  CHECK_EQ(b.at(0, 0), 0);
  a.paintRule(3, 3, filled);
  CHECK_EQ(a.at(3, 3), RuleBit::E | RuleBit::S);
  // the whole bottom row against the border
  TilemapEdit c;
  fresh(c, 4, 3);
  c.paintRuleRect(0, 2, 3, 2, filled);
  CHECK_EQ(c.at(0, 2), RuleBit::W | RuleBit::E | RuleBit::S);
  CHECK_EQ(c.at(1, 2), RuleBit::W | RuleBit::E | RuleBit::S);
  CHECK_EQ(c.neighbourMask(1, 2, filled), RuleBit::W | RuleBit::E | RuleBit::S);
  CHECK_EQ(c.neighbourMask(1, 2, empty), RuleBit::W | RuleBit::E);
  // refresh with the other setting re-picks every cell
  CHECK(c.refreshRule(empty) > 0);
  CHECK_EQ(c.at(0, 2), RuleBit::E);
  CHECK_EQ(c.at(1, 2), RuleBit::W | RuleBit::E);
  CHECK_EQ(c.at(3, 2), RuleBit::W);
}

TEST(ruletile_eight_neighbours_and_foreign_tiles) {
  RuleTile r;
  std::string err;
  CHECK(r.parse("{\"format\":1,\"neighbours\":8,\"outside\":\"empty\",\"default\":7,\"members\":[30],"
                "\"rules\":[{\"mask\":255,\"tile\":5},{\"mask\":15,\"care\":15,\"tile\":6}]}", err));
  TilemapEdit m;
  fresh(m, 7, 7);
  m.paintRuleRect(2, 2, 4, 4, r);
  CHECK_EQ(m.at(3, 3), 5);   // all eight neighbours
  CHECK_EQ(m.at(2, 2), 7);   // a corner of the block
  m.eraseRule(2, 2, r);      // remove the NW corner: the centre has four edges but a corner missing
  CHECK_EQ(m.at(3, 3), 6);
  // a foreign tile (not in the group) next to the stroke is neither changed nor counted
  m.paint(3, 5, 50);
  m.paintRule(3, 5 - 0, r);          // painting over it makes it a group tile
  CHECK(r.isRuleTile(m.at(3, 5)));
  m.paint(0, 0, 50);
  m.paintRule(1, 0, r);
  CHECK_EQ(m.at(0, 0), 50);          // foreign neighbour untouched
  CHECK_EQ(m.at(1, 0), 7);           // and it does not count as a neighbour
  // extra members count as neighbours but are never rewritten
  m.paint(6, 6, 30);
  m.paintRule(5, 6, r);
  CHECK_EQ(m.at(6, 6), 30);
  CHECK_EQ(m.neighbourMask(5, 6, r) & RuleBit::E, RuleBit::E);
}

TEST(ruletile_fill_rule) {
  RuleTile r = autotile4(false);
  TilemapEdit m;
  fresh(m, 5, 4);
  m.beginStroke();
  for (int y = 0; y < 4; ++y) m.paint(2, y, 40);
  m.endStroke();
  const size_t depth = m.undoStack().depth();
  CHECK(m.fillRule(0, 0, r) > 0);  // the empty region on the left: 2 x 4 cells
  CHECK_EQ(m.undoStack().depth(), depth + 1);
  CHECK_EQ(m.at(0, 0), RuleBit::E | RuleBit::S);
  CHECK_EQ(m.at(1, 1), 15 & ~RuleBit::E);  // the cell at x=2 is a foreign tile 40: not a neighbour
  CHECK_EQ(m.at(2, 1), 40);
  CHECK_EQ(m.at(3, 1), kTileEmpty);
  CHECK(m.undo());
  CHECK_EQ(m.at(0, 0), kTileEmpty);
}

TEST(tilemap_slopes_sorted_by_numeric_id) {
  TilemapEdit m;
  fresh(m);
  m.setTileProps(10, {TileKind::Slope, "45L"});
  m.setTileProps(9, {TileKind::Slope, "45R"});
  const std::string s = m.serialize();
  CHECK(s.find("\"9\"") < s.find("\"10\""));
}
