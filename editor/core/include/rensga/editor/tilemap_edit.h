// F4.08 (Editor 7): the tilemap model behind the Tile Palette and the 2D Scene view. No ImGui, no GL, no console names.
//
// A TilemapEdit owns one `*.tilemap.json` (format 1, read by the cooker and by scene2d's readTilemap):
//
//   { "format": 1, "tileset": "<texture GUID>", "tileSize": 32, "width": 40, "height": 14,
//     "tiles": [ ... width*height ids, row-major, 65535 = empty ... ],
//     "solid": [ids], "oneWay": [ids], "slopes": { "<id>": "45L"|"45R"|"22L_lo"|"22L_hi"|"22R_lo"|"22R_hi" } }
//
// Every key the editor does not know is kept and written back in place (key order is preserved). A file that is loaded
// and saved without edits keeps its exact bytes. Edited files are written with one space of indent, one array element
// per line, a trailing newline (the format the sample generators use), "solid"/"oneWay" sorted ascending and "slopes"
// sorted by id.
//
// Tile flags are per tile id, and an id has at most one of solid / oneWay / slope (the cooker rejects more).
//
// Strokes: beginStroke() ... paint*/erase*/fill* ... endStroke() is ONE undo Command ("Paint Tiles"...). Edits are applied
// to the map at once, so a view can draw them while the mouse is down. An edit made with no stroke open is its own stroke.
// Every other edit (flags, resize, tileset) is one Command too. Undo goes to the UndoStack set with setUndoStack(), by
// default the one this object owns; commands keep a pointer to the TilemapEdit, so it must outlive them.
//
// Rule tiles (`*.ruletile.json`, see RuleTile) place a tile chosen by the neighbours. Membership is derived from the tile
// ids alone, so the map file needs no extra data: a cell belongs to a rule tile when its id is one of the rule tile's ids.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/undo.h"

namespace rensga {
namespace editor {

constexpr uint16_t kTileEmpty = 0xFFFF;

// ---- tile flags ------------------------------------------------------------------------------------------------
enum class TileKind : uint8_t { None, Solid, OneWay, Slope };
struct TileProps {
  TileKind kind = TileKind::None;
  std::string slope;  // for Slope: one of kSlopeNames
  bool operator==(const TileProps& o) const { return kind == o.kind && slope == o.slope; }
  bool operator!=(const TileProps& o) const { return !(*this == o); }
};
constexpr const char* kSlopeNames[6] = {"45L", "45R", "22L_lo", "22L_hi", "22R_lo", "22R_hi"};
bool isSlopeName(const std::string& s);

// ---- rule tiles ------------------------------------------------------------------------------------------------
// File `*.ruletile.json`:
//
//   { "format": 1, "tileset": "<texture GUID>",
//     "neighbours": 4 | 8,            // optional, default 4: 4 = edges only, 8 = edges and corners
//     "outside": "filled" | "empty",  // optional, default "filled": how cells beyond the map border count
//     "default": 7,                   // tile used when no rule matches (and when the rule tile is first placed)
//     "members": [12, 13],            // optional: extra ids that belong to the group (not placed by the rules)
//     "rules": [ { "mask": 6, "tile": 0 },                 // first matching rule wins
//                { "mask": 255, "care": 15, "tile": 4 } ]  // "care" (optional): only these mask bits are compared
//   }
//
// The mask has one bit per neighbour that belongs to the group (RuleBit below). Bits 0..3 are the edges, bits 4..7 the
// corners; a 4-neighbour rule tile only uses bits 0..3. A rule matches when (neighbours & care) == (mask & care);
// "care" defaults to 0x0F (4 neighbours) or 0xFF (8). The group is `default` + every rule `tile` + `members`.
namespace RuleBit {
constexpr uint8_t N = 1, E = 2, S = 4, W = 8, NE = 16, SE = 32, SW = 64, NW = 128;
}
struct RuleTile {
  struct Rule { uint8_t mask = 0; uint8_t care = 0; uint16_t tile = 0; };
  std::string tileset;
  int neighbours = 4;  // 4 or 8
  bool outsideFilled = true;
  uint16_t defaultTile = 0;
  std::vector<Rule> rules;
  std::vector<uint16_t> members;

  bool parse(const std::string& text, std::string& err);
  bool load(const std::filesystem::path& file, std::string& err);
  std::string toText() const;                 // canonical text, parse(toText()) gives the same rule tile
  bool isMember(uint16_t tile) const;     // default, a rule tile or in `members`: counts as a neighbour
  bool isRuleTile(uint16_t tile) const;   // default or a rule tile: the ids the rules place (re-picked on refresh)
  uint16_t resolve(uint8_t neighbourMask) const;  // the tile for a neighbour mask (rules in order, else defaultTile)
  uint8_t careMask() const { return neighbours == 8 ? 0xFF : 0x0F; }
};

// ---- the tilemap ----------------------------------------------------------------------------------------------
class TilemapEdit {
 public:
  TilemapEdit();
  TilemapEdit(const TilemapEdit&) = delete;
  TilemapEdit& operator=(const TilemapEdit&) = delete;

  struct Cell { uint32_t index; uint16_t before, after; };  // one changed cell of a stroke (used by the undo commands)

  // ---- loading and saving
  // A new, empty, unsaved map (dirty until saved). Sizes are clamped to 1..kMaxSide.
  void create(const std::string& tilesetGuid, int tileSize, int width, int height);
  bool load(const std::filesystem::path& file, std::string& err);  // also sets path(); clears the owned undo stack
  bool parse(const std::string& text, std::string& err);
  bool save(std::string& err);                                       // to path()
  bool saveAs(const std::filesystem::path& file, std::string& err);
  // What save() writes: the loaded bytes while nothing changed, else serialize().
  std::string toText() const;
  // Always builds the text from the model (canonical formatting).
  std::string serialize() const;
  const std::filesystem::path& path() const { return path_; }
  bool dirty() const { return pos_ != cleanPos_ || !cleanValid_; }
  // Problems found while loading that did not stop it (short or long tiles array, an id with two flags...).
  const std::vector<std::string>& warnings() const { return warnings_; }
  // Counts every change to the content, including undo and redo. Views use it to know when to redraw.
  uint32_t revision() const { return rev_; }

  // ---- reading
  static constexpr int kMaxSide = 4096;
  const std::string& tileset() const { return tileset_; }
  int tileSize() const { return tileSize_; }
  int width() const { return w_; }
  int height() const { return h_; }
  bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
  uint16_t at(int x, int y) const { return inside(x, y) ? tiles_[static_cast<size_t>(y) * w_ + x] : kTileEmpty; }
  const std::vector<uint16_t>& tiles() const { return tiles_; }
  TileProps tileProps(uint16_t tile) const;
  // Every id with a flag, ascending.
  std::vector<std::pair<uint16_t, TileProps>> flaggedTiles() const;

  // ---- strokes (one undo Command per beginStroke ... endStroke)
  void beginStroke(const char* commandName = "Paint Tiles");
  // Closes the stroke. Returns true when it changed any cell (then exactly one Command was recorded).
  bool endStroke();
  bool inStroke() const { return stroking_; }

  // Each returns the number of cells written (0 = nothing changed, also for cells outside the map). Rect corners may be
  // given in any order and are clipped to the map. `tile` kTileEmpty erases.
  int paint(int x, int y, uint16_t tile);
  int paintRect(int x0, int y0, int x1, int y1, uint16_t tile);
  int erase(int x, int y) { return paint(x, y, kTileEmpty); }
  int eraseRect(int x0, int y0, int x1, int y1) { return paintRect(x0, y0, x1, y1, kTileEmpty); }
  // 4-connected flood fill of the cells that have the same id as (x, y).
  int fill(int x, int y, uint16_t tile);

  // Rule brushes: place or erase cells, then re-pick the tile of every cell of the group next to them.
  int paintRule(int x, int y, const RuleTile& rule);
  int eraseRule(int x, int y, const RuleTile& rule);
  int paintRuleRect(int x0, int y0, int x1, int y1, const RuleTile& rule);
  int eraseRuleRect(int x0, int y0, int x1, int y1, const RuleTile& rule);
  int fillRule(int x, int y, const RuleTile& rule);
  // Re-picks every cell of the group (for example after the rule tile file changed).
  int refreshRule(const RuleTile& rule);
  // The neighbour mask (RuleBit) of a cell for a rule tile.
  uint8_t neighbourMask(int x, int y, const RuleTile& rule) const;

  // ---- other edits (each one Command)
  // At most one of solid / oneWay / slope per id; TileKind::None clears. False for an unknown slope name.
  bool setTileProps(uint16_t tile, const TileProps& props);
  // Resizes the map; the old content moves by (offsetX, offsetY) cells, new cells are empty. False for a bad size.
  bool resize(int width, int height, int offsetX = 0, int offsetY = 0);
  // Changes the tileset GUID (and the tile size when tileSize > 0).
  void setTileset(const std::string& tilesetGuid, int tileSize = 0);

  // ---- undo
  void setUndoStack(UndoStack* stack) { stack_ = stack ? stack : &own_; }
  UndoStack& undoStack() { return *stack_; }
  bool undo();
  bool redo();
  bool canUndo() const { return stack_->canUndo(); }
  bool canRedo() const { return stack_->canRedo(); }

 private:
  friend struct TilemapEditAccess;

  bool setCell(size_t index, uint16_t tile);
  void recordCommand(std::unique_ptr<Command> c);
  int ruleApply(const std::vector<size_t>& cells, const RuleTile& rule, bool erase);
  void rectCells(int x0, int y0, int x1, int y1, std::vector<size_t>& out) const;
  void floodCells(int x, int y, std::vector<size_t>& out) const;
  void markSaved(const std::string& text);
  void resetModel();

  nlohmann::ordered_json doc_ = nlohmann::ordered_json::object();  // loaded document: carries the unknown keys
  std::string tileset_;
  int tileSize_ = 16, w_ = 0, h_ = 0;
  std::vector<uint16_t> tiles_;
  std::map<uint16_t, TileProps> props_;  // only ids with a flag
  std::filesystem::path path_;
  std::string savedText_;
  std::vector<std::string> warnings_;
  uint32_t rev_ = 0;

  UndoStack own_;
  UndoStack* stack_ = &own_;
  int pos_ = 0, cleanPos_ = 0;  // commands applied since load; position of the saved state
  bool cleanValid_ = true;

  bool stroking_ = false;
  std::string strokeName_;
  std::map<uint32_t, uint16_t> strokeBefore_;  // cell -> id when the stroke first touched it
};

}  // namespace editor
}  // namespace rensga
