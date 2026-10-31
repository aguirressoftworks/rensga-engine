#include "rensga/editor/tilemap_edit.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

namespace rensga {
namespace editor {

using nlohmann::ordered_json;

bool isSlopeName(const std::string& s) {
  for (const char* n : kSlopeNames)
    if (s == n) return true;
  return false;
}

// ---- RuleTile ----------------------------------------------------------------------------------------------------
namespace {
bool intIn(const ordered_json& v, long long lo, long long hi, long long& out) {
  if (!v.is_number_integer()) return false;
  out = v.get<long long>();
  return out >= lo && out <= hi;
}
}  // namespace

bool RuleTile::parse(const std::string& text, std::string& err) {
  ordered_json j = ordered_json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) { err = "not a JSON object"; return false; }
  long long v = 0;
  if (!j.contains("format") || !intIn(j["format"], 1, 1, v)) { err = "unsupported or missing \"format\" (expected 1)"; return false; }
  RuleTile r;
  if (j.contains("tileset")) {
    if (!j["tileset"].is_string()) { err = "\"tileset\" must be a GUID string"; return false; }
    r.tileset = j["tileset"].get<std::string>();
  }
  if (j.contains("neighbours")) {
    if (!intIn(j["neighbours"], 4, 8, v) || (v != 4 && v != 8)) { err = "\"neighbours\" must be 4 or 8"; return false; }
    r.neighbours = static_cast<int>(v);
  }
  if (j.contains("outside")) {
    if (!j["outside"].is_string() || (j["outside"] != "filled" && j["outside"] != "empty")) {
      err = "\"outside\" must be \"filled\" or \"empty\"";
      return false;
    }
    r.outsideFilled = j["outside"] == "filled";
  }
  if (!j.contains("default") || !intIn(j["default"], 0, 65534, v)) { err = "\"default\" must be a tile id (0..65534)"; return false; }
  r.defaultTile = static_cast<uint16_t>(v);
  if (j.contains("members")) {
    if (!j["members"].is_array()) { err = "\"members\" must be an array of tile ids"; return false; }
    for (const auto& m : j["members"]) {
      if (!intIn(m, 0, 65534, v)) { err = "\"members\" holds a value that is not a tile id"; return false; }
      r.members.push_back(static_cast<uint16_t>(v));
    }
  }
  if (j.contains("rules")) {
    if (!j["rules"].is_array()) { err = "\"rules\" must be an array"; return false; }
    int i = 0;
    for (const auto& e : j["rules"]) {
      const std::string where = "rules[" + std::to_string(i++) + "]";
      Rule rule;
      rule.care = r.careMask();
      if (!e.is_object()) { err = where + " must be an object"; return false; }
      if (!e.contains("mask") || !intIn(e["mask"], 0, 255, v)) { err = where + ".mask must be 0..255"; return false; }
      rule.mask = static_cast<uint8_t>(v);
      if (e.contains("care")) {
        if (!intIn(e["care"], 0, 255, v)) { err = where + ".care must be 0..255"; return false; }
        rule.care = static_cast<uint8_t>(v);
      }
      if (!e.contains("tile") || !intIn(e["tile"], 0, 65534, v)) { err = where + ".tile must be a tile id (0..65534)"; return false; }
      rule.tile = static_cast<uint16_t>(v);
      r.rules.push_back(rule);
    }
  }
  *this = std::move(r);
  return true;
}

bool RuleTile::load(const std::filesystem::path& file, std::string& err) {
  std::ifstream in(file, std::ios::binary);
  if (!in) { err = "cannot read " + file.string(); return false; }
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse(ss.str(), err);
}

std::string RuleTile::toText() const {
  ordered_json j = ordered_json::object();
  j["format"] = 1;
  j["tileset"] = tileset;
  j["neighbours"] = neighbours;
  j["outside"] = outsideFilled ? "filled" : "empty";
  j["default"] = static_cast<unsigned>(defaultTile);
  if (!members.empty()) {
    ordered_json a = ordered_json::array();
    for (uint16_t m : members) a.push_back(static_cast<unsigned>(m));
    j["members"] = a;
  }
  ordered_json rs = ordered_json::array();
  for (const Rule& r : rules) {
    ordered_json o = ordered_json::object();
    o["mask"] = static_cast<unsigned>(r.mask);
    if (r.care != careMask()) o["care"] = static_cast<unsigned>(r.care);
    o["tile"] = static_cast<unsigned>(r.tile);
    rs.push_back(o);
  }
  j["rules"] = rs;
  return j.dump(1) + "\n";
}

bool RuleTile::isRuleTile(uint16_t t) const {
  if (t == defaultTile) return true;
  for (const Rule& r : rules)
    if (r.tile == t) return true;
  return false;
}

bool RuleTile::isMember(uint16_t t) const {
  if (t == kTileEmpty) return false;
  if (isRuleTile(t)) return true;
  return std::find(members.begin(), members.end(), t) != members.end();
}

uint16_t RuleTile::resolve(uint8_t mask) const {
  for (const Rule& r : rules)
    if ((mask & r.care) == (r.mask & r.care)) return r.tile;
  return defaultTile;
}

// ---- undo commands -----------------------------------------------------------------------------------------------
struct TilemapEditAccess {
  static void cells(TilemapEdit& e, const std::vector<TilemapEdit::Cell>& c, bool useAfter) {
    for (const auto& k : c) e.tiles_[k.index] = useAfter ? k.after : k.before;
    ++e.rev_;
  }
  static void props(TilemapEdit& e, uint16_t id, const TileProps& p) {
    if (p.kind == TileKind::None) e.props_.erase(id);
    else e.props_[id] = p;
    ++e.rev_;
  }
  static void shape(TilemapEdit& e, int w, int h, const std::vector<uint16_t>& t) {
    e.w_ = w;
    e.h_ = h;
    e.tiles_ = t;
    ++e.rev_;
  }
  static void meta(TilemapEdit& e, const std::string& tileset, int tileSize) {
    e.tileset_ = tileset;
    e.tileSize_ = tileSize;
    ++e.rev_;
  }
  static void step(TilemapEdit& e, int d) { e.pos_ += d; }
};

namespace {

class StrokeCmd : public Command {
 public:
  StrokeCmd(TilemapEdit* e, std::string name, std::vector<TilemapEdit::Cell> cells)
      : e_(e), name_(std::move(name)), cells_(std::move(cells)) {}
  const char* name() const override { return name_.c_str(); }
  void undo() override { TilemapEditAccess::cells(*e_, cells_, false); TilemapEditAccess::step(*e_, -1); }
  void redo() override { TilemapEditAccess::cells(*e_, cells_, true); TilemapEditAccess::step(*e_, 1); }

 private:
  TilemapEdit* e_;
  std::string name_;
  std::vector<TilemapEdit::Cell> cells_;
};

class PropsCmd : public Command {
 public:
  PropsCmd(TilemapEdit* e, uint16_t id, TileProps before, TileProps after)
      : e_(e), id_(id), before_(std::move(before)), after_(std::move(after)) {}
  const char* name() const override { return "Edit Tile Flags"; }
  void undo() override { TilemapEditAccess::props(*e_, id_, before_); TilemapEditAccess::step(*e_, -1); }
  void redo() override { TilemapEditAccess::props(*e_, id_, after_); TilemapEditAccess::step(*e_, 1); }

 private:
  TilemapEdit* e_;
  uint16_t id_;
  TileProps before_, after_;
};

struct Shape { int w = 0, h = 0; std::vector<uint16_t> tiles; };

class ResizeCmd : public Command {
 public:
  ResizeCmd(TilemapEdit* e, Shape before, Shape after) : e_(e), before_(std::move(before)), after_(std::move(after)) {}
  const char* name() const override { return "Resize Tilemap"; }
  void undo() override { TilemapEditAccess::shape(*e_, before_.w, before_.h, before_.tiles); TilemapEditAccess::step(*e_, -1); }
  void redo() override { TilemapEditAccess::shape(*e_, after_.w, after_.h, after_.tiles); TilemapEditAccess::step(*e_, 1); }

 private:
  TilemapEdit* e_;
  Shape before_, after_;
};

class MetaCmd : public Command {
 public:
  MetaCmd(TilemapEdit* e, std::string gb, int sb, std::string ga, int sa)
      : e_(e), gb_(std::move(gb)), sb_(sb), ga_(std::move(ga)), sa_(sa) {}
  const char* name() const override { return "Change Tileset"; }
  void undo() override { TilemapEditAccess::meta(*e_, gb_, sb_); TilemapEditAccess::step(*e_, -1); }
  void redo() override { TilemapEditAccess::meta(*e_, ga_, sa_); TilemapEditAccess::step(*e_, 1); }

 private:
  TilemapEdit* e_;
  std::string gb_;
  int sb_;
  std::string ga_;
  int sa_;
};

}  // namespace

// ---- TilemapEdit --------------------------------------------------------------------------------------------------
TilemapEdit::TilemapEdit() = default;

void TilemapEdit::resetModel() {
  doc_ = ordered_json::object();
  tileset_.clear();
  tileSize_ = 16;
  w_ = h_ = 0;
  tiles_.clear();
  props_.clear();
  warnings_.clear();
  savedText_.clear();
  stroking_ = false;
  strokeBefore_.clear();
  pos_ = cleanPos_ = 0;
  cleanValid_ = true;
  if (stack_ == &own_) own_.clear();
  ++rev_;
}

void TilemapEdit::create(const std::string& tilesetGuid, int tileSize, int width, int height) {
  resetModel();
  path_.clear();
  tileset_ = tilesetGuid;
  tileSize_ = std::max(1, tileSize);
  w_ = std::clamp(width, 1, kMaxSide);
  h_ = std::clamp(height, 1, kMaxSide);
  tiles_.assign(static_cast<size_t>(w_) * h_, kTileEmpty);
  cleanValid_ = false;  // never saved
}

void TilemapEdit::markSaved(const std::string& text) {
  savedText_ = text;
  cleanPos_ = pos_;
  cleanValid_ = true;
}

bool TilemapEdit::parse(const std::string& text, std::string& err) {
  err.clear();
  ordered_json j = ordered_json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) { err = "not a JSON object"; return false; }
  long long v = 0;
  if (!j.contains("format") || !intIn(j["format"], 1, 1, v)) { err = "unsupported or missing \"format\" (expected 1)"; return false; }
  std::string tileset;
  if (j.contains("tileset")) {
    if (!j["tileset"].is_string()) { err = "\"tileset\" must be a GUID string"; return false; }
    tileset = j["tileset"].get<std::string>();
  }
  long long ts = 0, w = 0, h = 0;
  if (!j.contains("tileSize") || !intIn(j["tileSize"], 1, 4096, ts)) { err = "\"tileSize\" must be 1..4096"; return false; }
  if (!j.contains("width") || !intIn(j["width"], 1, kMaxSide, w)) { err = "\"width\" must be 1.." + std::to_string(kMaxSide); return false; }
  if (!j.contains("height") || !intIn(j["height"], 1, kMaxSide, h)) { err = "\"height\" must be 1.." + std::to_string(kMaxSide); return false; }
  if (!j.contains("tiles") || !j["tiles"].is_array()) { err = "\"tiles\" must be an array"; return false; }
  std::vector<std::string> warn;
  std::vector<uint16_t> tiles;
  tiles.reserve(j["tiles"].size());
  for (const auto& t : j["tiles"]) {
    if (!intIn(t, -1, 65535, v)) { err = "\"tiles\" holds a value outside -1..65535"; return false; }
    tiles.push_back(v < 0 ? kTileEmpty : static_cast<uint16_t>(v));
  }
  const size_t want = static_cast<size_t>(w) * static_cast<size_t>(h);
  if (tiles.size() != want) {
    warn.push_back("tiles has " + std::to_string(tiles.size()) + " entries, width*height is " + std::to_string(want) +
                   " (padded with empty cells or cut)");
    tiles.resize(want, kTileEmpty);
  }
  std::map<uint16_t, TileProps> props;
  auto addFlag = [&](long long id, TileKind kind, const std::string& slope, const char* what) {
    if (id < 0 || id > 65534) { err = std::string(what) + " holds a value that is not a tile id"; return false; }
    auto& p = props[static_cast<uint16_t>(id)];
    if (p.kind != TileKind::None) {
      warn.push_back("tile id " + std::to_string(id) + " has more than one of solid, oneWay and slopes; the first one is kept");
      return true;
    }
    p.kind = kind;
    p.slope = slope;
    return true;
  };
  if (j.contains("solid")) {
    if (!j["solid"].is_array()) { err = "\"solid\" must be an array of tile ids"; return false; }
    for (const auto& e : j["solid"]) {
      if (!e.is_number_integer()) { err = "\"solid\" holds a value that is not a tile id"; return false; }
      if (!addFlag(e.get<long long>(), TileKind::Solid, "", "\"solid\"")) return false;
    }
  }
  if (j.contains("oneWay")) {
    if (!j["oneWay"].is_array()) { err = "\"oneWay\" must be an array of tile ids"; return false; }
    for (const auto& e : j["oneWay"]) {
      if (!e.is_number_integer()) { err = "\"oneWay\" holds a value that is not a tile id"; return false; }
      if (!addFlag(e.get<long long>(), TileKind::OneWay, "", "\"oneWay\"")) return false;
    }
  }
  if (j.contains("slopes")) {
    if (!j["slopes"].is_object()) { err = "\"slopes\" must be an object of id -> slope name"; return false; }
    for (auto it = j["slopes"].begin(); it != j["slopes"].end(); ++it) {
      char* end = nullptr;
      const long long id = std::strtoll(it.key().c_str(), &end, 10);
      if (it.key().empty() || *end) { err = "\"slopes\" key \"" + it.key() + "\" is not a tile id"; return false; }
      if (!it.value().is_string() || !isSlopeName(it.value().get<std::string>())) {
        err = "\"slopes\" value for tile " + it.key() + " is not one of 45L, 45R, 22L_lo, 22L_hi, 22R_lo, 22R_hi";
        return false;
      }
      if (!addFlag(id, TileKind::Slope, it.value().get<std::string>(), "\"slopes\"")) return false;
    }
  }
  resetModel();
  doc_ = std::move(j);
  tileset_ = tileset;
  tileSize_ = static_cast<int>(ts);
  w_ = static_cast<int>(w);
  h_ = static_cast<int>(h);
  tiles_ = std::move(tiles);
  props_ = std::move(props);
  warnings_ = std::move(warn);
  markSaved(text);
  return true;
}

bool TilemapEdit::load(const std::filesystem::path& file, std::string& err) {
  std::ifstream in(file, std::ios::binary);
  if (!in) { err = "cannot read " + file.string(); return false; }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (!parse(ss.str(), err)) { err = file.filename().string() + ": " + err; return false; }
  path_ = file;
  return true;
}

std::string TilemapEdit::serialize() const {
  ordered_json j = doc_.is_object() ? doc_ : ordered_json::object();
  j["format"] = 1;
  j["tileset"] = tileset_;
  j["tileSize"] = tileSize_;
  j["width"] = w_;
  j["height"] = h_;
  ordered_json tiles = ordered_json::array();
  for (uint16_t t : tiles_) tiles.push_back(static_cast<unsigned>(t));
  j["tiles"] = tiles;
  ordered_json solid = ordered_json::array(), oneWay = ordered_json::array(), slopes = ordered_json::object();
  for (const auto& kv : props_) {  // std::map: ascending ids
    if (kv.second.kind == TileKind::Solid) solid.push_back(static_cast<unsigned>(kv.first));
    else if (kv.second.kind == TileKind::OneWay) oneWay.push_back(static_cast<unsigned>(kv.first));
    else if (kv.second.kind == TileKind::Slope) slopes[std::to_string(kv.first)] = kv.second.slope;
  }
  if (!solid.empty() || j.contains("solid")) j["solid"] = solid;
  if (!oneWay.empty() || j.contains("oneWay")) j["oneWay"] = oneWay;
  if (!slopes.empty() || j.contains("slopes")) j["slopes"] = slopes;
  return j.dump(1) + "\n";
}

std::string TilemapEdit::toText() const { return (!dirty() && !savedText_.empty()) ? savedText_ : serialize(); }

bool TilemapEdit::saveAs(const std::filesystem::path& file, std::string& err) {
  const std::string text = toText();
  const std::filesystem::path tmp = file.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) { err = "cannot write " + tmp.string(); return false; }
    out << text;
    out.close();
    if (!out) { err = "write failed: " + tmp.string(); return false; }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, file, ec);
  if (ec) { std::filesystem::remove(tmp, ec); err = "cannot replace " + file.string(); return false; }
  path_ = file;
  markSaved(text);
  return true;
}

bool TilemapEdit::save(std::string& err) {
  if (path_.empty()) { err = "no file path (use saveAs)"; return false; }
  return saveAs(path_, err);
}

TileProps TilemapEdit::tileProps(uint16_t tile) const {
  auto it = props_.find(tile);
  return it == props_.end() ? TileProps() : it->second;
}

std::vector<std::pair<uint16_t, TileProps>> TilemapEdit::flaggedTiles() const {
  return std::vector<std::pair<uint16_t, TileProps>>(props_.begin(), props_.end());
}

// ---- strokes ------------------------------------------------------------------------------------------------------
void TilemapEdit::recordCommand(std::unique_ptr<Command> c) {
  if (cleanPos_ > pos_) cleanValid_ = false;  // the redo branch that held the saved state is dropped
  stack_->record(std::move(c));
  ++pos_;
}

void TilemapEdit::beginStroke(const char* commandName) {
  if (stroking_) endStroke();
  stroking_ = true;
  strokeName_ = commandName ? commandName : "Paint Tiles";
  strokeBefore_.clear();
}

bool TilemapEdit::endStroke() {
  if (!stroking_) return false;
  stroking_ = false;
  std::vector<Cell> cells;
  for (const auto& kv : strokeBefore_)
    if (tiles_[kv.first] != kv.second) cells.push_back({kv.first, kv.second, tiles_[kv.first]});
  strokeBefore_.clear();
  if (cells.empty()) return false;
  recordCommand(std::make_unique<StrokeCmd>(this, strokeName_, std::move(cells)));
  return true;
}

bool TilemapEdit::setCell(size_t index, uint16_t tile) {
  if (index >= tiles_.size() || tiles_[index] == tile) return false;
  strokeBefore_.emplace(static_cast<uint32_t>(index), tiles_[index]);  // keeps the first value
  tiles_[index] = tile;
  ++rev_;
  return true;
}

void TilemapEdit::rectCells(int x0, int y0, int x1, int y1, std::vector<size_t>& out) const {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  x0 = std::max(x0, 0); y0 = std::max(y0, 0);
  x1 = std::min(x1, w_ - 1); y1 = std::min(y1, h_ - 1);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) out.push_back(static_cast<size_t>(y) * w_ + x);
}

void TilemapEdit::floodCells(int x, int y, std::vector<size_t>& out) const {
  if (!inside(x, y)) return;
  const uint16_t target = at(x, y);
  std::vector<char> seen(tiles_.size(), 0);
  std::vector<size_t> todo;
  todo.push_back(static_cast<size_t>(y) * w_ + x);
  seen[todo.back()] = 1;
  while (!todo.empty()) {
    const size_t i = todo.back();
    todo.pop_back();
    out.push_back(i);
    const int cx = static_cast<int>(i % w_), cy = static_cast<int>(i / w_);
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
      const int nx = cx + dx[k], ny = cy + dy[k];
      if (!inside(nx, ny)) continue;
      const size_t ni = static_cast<size_t>(ny) * w_ + nx;
      if (!seen[ni] && tiles_[ni] == target) { seen[ni] = 1; todo.push_back(ni); }
    }
  }
  std::sort(out.begin(), out.end());
}

namespace {
// Opens a one-edit stroke when none is open.
struct AutoStroke {
  TilemapEdit& e;
  bool own;
  AutoStroke(TilemapEdit& m, const char* name) : e(m), own(!m.inStroke()) { if (own) e.beginStroke(name); }
  ~AutoStroke() { if (own) e.endStroke(); }
};
}  // namespace

int TilemapEdit::paintRect(int x0, int y0, int x1, int y1, uint16_t tile) {
  std::vector<size_t> cells;
  rectCells(x0, y0, x1, y1, cells);
  AutoStroke s(*this, tile == kTileEmpty ? "Erase Tiles" : "Paint Tiles");
  int n = 0;
  for (size_t c : cells)
    if (setCell(c, tile)) ++n;
  return n;
}

int TilemapEdit::paint(int x, int y, uint16_t tile) { return paintRect(x, y, x, y, tile); }

int TilemapEdit::fill(int x, int y, uint16_t tile) {
  if (!inside(x, y) || at(x, y) == tile) return 0;
  std::vector<size_t> cells;
  floodCells(x, y, cells);
  AutoStroke s(*this, "Fill Tiles");
  int n = 0;
  for (size_t c : cells)
    if (setCell(c, tile)) ++n;
  return n;
}

// ---- rule tiles ---------------------------------------------------------------------------------------------------
uint8_t TilemapEdit::neighbourMask(int x, int y, const RuleTile& r) const {
  struct Dir { int dx, dy; uint8_t bit; };
  static const Dir dirs[8] = {{0, -1, RuleBit::N}, {1, 0, RuleBit::E}, {0, 1, RuleBit::S}, {-1, 0, RuleBit::W},
                              {1, -1, RuleBit::NE}, {1, 1, RuleBit::SE}, {-1, 1, RuleBit::SW}, {-1, -1, RuleBit::NW}};
  uint8_t mask = 0;
  const int count = r.neighbours == 8 ? 8 : 4;
  for (int i = 0; i < count; ++i) {
    const int nx = x + dirs[i].dx, ny = y + dirs[i].dy;
    const bool filled = inside(nx, ny) ? r.isMember(at(nx, ny)) : r.outsideFilled;
    if (filled) mask |= dirs[i].bit;
  }
  return mask;
}

int TilemapEdit::ruleApply(const std::vector<size_t>& cells, const RuleTile& rule, bool erase) {
  AutoStroke s(*this, erase ? "Erase Rule Tiles" : "Paint Rule Tiles");
  int n = 0;
  for (size_t c : cells)
    if (setCell(c, erase ? kTileEmpty : rule.defaultTile)) ++n;
  std::set<size_t> affected;
  const bool diag = rule.neighbours == 8;
  for (size_t c : cells) {
    const int cx = static_cast<int>(c % w_), cy = static_cast<int>(c / w_);
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (!diag && dx && dy) continue;
        if (inside(cx + dx, cy + dy)) affected.insert(static_cast<size_t>(cy + dy) * w_ + (cx + dx));
      }
  }
  for (size_t i : affected) {
    if (!rule.isRuleTile(tiles_[i])) continue;
    const int x = static_cast<int>(i % w_), y = static_cast<int>(i / w_);
    if (setCell(i, rule.resolve(neighbourMask(x, y, rule)))) ++n;
  }
  return n;
}

int TilemapEdit::paintRuleRect(int x0, int y0, int x1, int y1, const RuleTile& rule) {
  std::vector<size_t> cells;
  rectCells(x0, y0, x1, y1, cells);
  return ruleApply(cells, rule, false);
}
int TilemapEdit::eraseRuleRect(int x0, int y0, int x1, int y1, const RuleTile& rule) {
  std::vector<size_t> cells;
  rectCells(x0, y0, x1, y1, cells);
  return ruleApply(cells, rule, true);
}
int TilemapEdit::paintRule(int x, int y, const RuleTile& rule) { return paintRuleRect(x, y, x, y, rule); }
int TilemapEdit::eraseRule(int x, int y, const RuleTile& rule) { return eraseRuleRect(x, y, x, y, rule); }

int TilemapEdit::fillRule(int x, int y, const RuleTile& rule) {
  std::vector<size_t> cells;
  floodCells(x, y, cells);
  return ruleApply(cells, rule, false);
}

int TilemapEdit::refreshRule(const RuleTile& rule) {
  AutoStroke s(*this, "Refresh Rule Tiles");
  // Resolve against a snapshot of membership: membership does not change when a rule tile id is swapped for another.
  int n = 0;
  for (size_t i = 0; i < tiles_.size(); ++i) {
    if (!rule.isRuleTile(tiles_[i])) continue;
    const int x = static_cast<int>(i % w_), y = static_cast<int>(i / w_);
    if (setCell(i, rule.resolve(neighbourMask(x, y, rule)))) ++n;
  }
  return n;
}

// ---- other edits --------------------------------------------------------------------------------------------------
bool TilemapEdit::setTileProps(uint16_t tile, const TileProps& props) {
  if (tile == kTileEmpty) return false;
  if (props.kind == TileKind::Slope && !isSlopeName(props.slope)) return false;
  TileProps next = props;
  if (next.kind != TileKind::Slope) next.slope.clear();
  const TileProps before = tileProps(tile);
  if (before == next) return true;
  endStroke();
  TilemapEditAccess::props(*this, tile, next);
  recordCommand(std::make_unique<PropsCmd>(this, tile, before, next));
  return true;
}

bool TilemapEdit::resize(int width, int height, int offsetX, int offsetY) {
  if (width < 1 || height < 1 || width > kMaxSide || height > kMaxSide) return false;
  if (width == w_ && height == h_ && offsetX == 0 && offsetY == 0) return true;
  endStroke();
  Shape before{w_, h_, tiles_}, after;
  after.w = width;
  after.h = height;
  after.tiles.assign(static_cast<size_t>(width) * height, kTileEmpty);
  for (int y = 0; y < h_; ++y)
    for (int x = 0; x < w_; ++x) {
      const int nx = x + offsetX, ny = y + offsetY;
      if (nx >= 0 && ny >= 0 && nx < width && ny < height) after.tiles[static_cast<size_t>(ny) * width + nx] = tiles_[static_cast<size_t>(y) * w_ + x];
    }
  TilemapEditAccess::shape(*this, after.w, after.h, after.tiles);
  recordCommand(std::make_unique<ResizeCmd>(this, std::move(before), std::move(after)));
  return true;
}

void TilemapEdit::setTileset(const std::string& tilesetGuid, int tileSize) {
  const int size = tileSize > 0 ? tileSize : tileSize_;
  if (tilesetGuid == tileset_ && size == tileSize_) return;
  endStroke();
  const std::string gb = tileset_;
  const int sb = tileSize_;
  TilemapEditAccess::meta(*this, tilesetGuid, size);
  recordCommand(std::make_unique<MetaCmd>(this, gb, sb, tilesetGuid, size));
}

bool TilemapEdit::undo() {
  endStroke();
  return stack_->undo();
}
bool TilemapEdit::redo() {
  endStroke();
  return stack_->redo();
}

}  // namespace editor
}  // namespace rensga
