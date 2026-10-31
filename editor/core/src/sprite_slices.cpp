#include "rensga/editor/sprite_slices.h"

#include <algorithm>

#include "rensga/core/gameplay.h"
#include "rensga/editor/asset_database.h"

namespace rensga {
namespace editor {

using nlohmann::json;

bool SpriteSlices::operator==(const SpriteSlices& o) const {
  return mode == o.mode && cellW == o.cellW && cellH == o.cellH && paddingX == o.paddingX && paddingY == o.paddingY &&
         offsetX == o.offsetX && offsetY == o.offsetY && rects == o.rects;
}

// ---- JSON ---------------------------------------------------------------------------------------------------------
namespace {
bool getInt(const json& o, const char* key, int& out, bool required, std::string& err, const std::string& where) {
  if (!o.contains(key)) {
    if (required) { err = where + "." + key + " is missing"; return false; }
    return true;
  }
  if (!o[key].is_number_integer()) { err = where + "." + key + " must be an integer"; return false; }
  out = o[key].get<int>();
  return true;
}

// A number (both axes) or [x, y].
bool getPair(const json& o, const char* key, int& a, int& b, std::string& err) {
  if (!o.contains(key)) return true;
  const json& v = o[key];
  if (v.is_number_integer()) { a = b = v.get<int>(); return true; }
  if (v.is_array() && v.size() == 2 && v[0].is_number_integer() && v[1].is_number_integer()) {
    a = v[0].get<int>();
    b = v[1].get<int>();
    return true;
  }
  err = std::string("sprites.") + key + " must be a number or [x, y]";
  return false;
}
}  // namespace

json SpriteSlices::toJson() const {
  json j = json::object();
  j["mode"] = mode == SliceMode::Grid ? "grid" : "manual";
  if (mode == SliceMode::Grid) {
    j["cellW"] = cellW;
    j["cellH"] = cellH;
    if (paddingX == paddingY) j["padding"] = paddingX;
    else j["padding"] = json::array({paddingX, paddingY});
    j["offset"] = json::array({offsetX, offsetY});
  }
  json rs = json::array();
  for (const SpriteRect& r : rects) {
    json o = json::object();
    o["name"] = r.name;
    o["x"] = r.x;
    o["y"] = r.y;
    o["w"] = r.w;
    o["h"] = r.h;
    o["pivotX"] = r.pivotX;
    o["pivotY"] = r.pivotY;
    rs.push_back(o);
  }
  j["rects"] = rs;
  return j;
}

bool SpriteSlices::fromJson(const json& j, SpriteSlices& out, std::string& err) {
  err.clear();
  if (!j.is_object()) { err = "sprites must be an object"; return false; }
  SpriteSlices s;
  const std::string mode = j.value("mode", std::string());
  if (mode == "grid") s.mode = SliceMode::Grid;
  else if (mode == "manual") s.mode = SliceMode::Manual;
  else { err = "sprites.mode must be \"grid\" or \"manual\""; return false; }
  if (s.mode == SliceMode::Grid) {
    if (!getInt(j, "cellW", s.cellW, true, err, "sprites") || !getInt(j, "cellH", s.cellH, true, err, "sprites")) return false;
    if (s.cellW < 1 || s.cellH < 1) { err = "sprites.cellW and cellH must be at least 1"; return false; }
    if (!getPair(j, "padding", s.paddingX, s.paddingY, err) || !getPair(j, "offset", s.offsetX, s.offsetY, err)) return false;
    if (s.paddingX < 0 || s.paddingY < 0 || s.offsetX < 0 || s.offsetY < 0) { err = "sprites.padding and offset cannot be negative"; return false; }
  }
  if (j.contains("rects")) {
    if (!j["rects"].is_array()) { err = "sprites.rects must be an array"; return false; }
    int i = 0;
    for (const json& e : j["rects"]) {
      const std::string where = "sprites.rects[" + std::to_string(i++) + "]";
      if (!e.is_object()) { err = where + " must be an object"; return false; }
      SpriteRect r;
      if (!e.contains("name") || !e["name"].is_string() || e["name"].get<std::string>().empty()) { err = where + ".name must be a non-empty string"; return false; }
      r.name = e["name"].get<std::string>();
      if (!getInt(e, "x", r.x, true, err, where) || !getInt(e, "y", r.y, true, err, where) || !getInt(e, "w", r.w, true, err, where) ||
          !getInt(e, "h", r.h, true, err, where) || !getInt(e, "pivotX", r.pivotX, false, err, where) ||
          !getInt(e, "pivotY", r.pivotY, false, err, where))
        return false;
      if (r.w < 1 || r.h < 1) { err = where + " has a width or height below 1"; return false; }
      s.rects.push_back(r);
    }
  }
  out = std::move(s);
  return true;
}

// ---- grid ---------------------------------------------------------------------------------------------------------
int SpriteSlices::gridColumns(int texW, int cellW, int paddingX, int offsetX) {
  if (cellW < 1 || texW - offsetX < cellW) return 0;
  return (texW - offsetX + paddingX) / (cellW + paddingX);
}
int SpriteSlices::gridRows(int texH, int cellH, int paddingY, int offsetY) { return gridColumns(texH, cellH, paddingY, offsetY); }

int SpriteSlices::generateGrid(int texW, int texH, const std::string& namePrefix, bool keepExisting) {
  mode = SliceMode::Grid;
  const int cols = gridColumns(texW, cellW, paddingX, offsetX), rows = gridRows(texH, cellH, paddingY, offsetY);
  std::vector<SpriteRect> old;
  if (keepExisting) old = rects;
  rects.clear();
  for (int i = 0; i < cols * rows; ++i) {
    SpriteRect r;
    r.x = offsetX + (i % cols) * (cellW + paddingX);
    r.y = offsetY + (i / cols) * (cellH + paddingY);
    r.w = cellW;
    r.h = cellH;
    if (i < static_cast<int>(old.size())) {
      r.name = old[i].name;
      r.pivotX = old[i].pivotX;
      r.pivotY = old[i].pivotY;
    }
    rects.push_back(r);
  }
  // Names of kept rectangles first, so a generated name never takes one of them.
  for (int i = 0; i < static_cast<int>(rects.size()); ++i) {
    if (!rects[i].name.empty()) continue;
    std::string n = namePrefix + "_" + std::to_string(i);
    for (int k = 2; find(n) >= 0; ++k) n = namePrefix + "_" + std::to_string(i) + "_" + std::to_string(k);
    rects[i].name = n;
  }
  return static_cast<int>(rects.size());
}

// ---- rectangles ---------------------------------------------------------------------------------------------------
int SpriteSlices::find(const std::string& name) const {
  for (size_t i = 0; i < rects.size(); ++i)
    if (rects[i].name == name) return static_cast<int>(i);
  return -1;
}

int SpriteSlices::indexAt(int px, int py) const {
  for (int i = static_cast<int>(rects.size()) - 1; i >= 0; --i) {
    const SpriteRect& r = rects[i];
    if (px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h) return i;
  }
  return -1;
}

int SpriteSlices::indexOfSrc(int x, int y, int w, int h) const {
  for (size_t i = 0; i < rects.size(); ++i)
    if (rects[i].x == x && rects[i].y == y && rects[i].w == w && rects[i].h == h) return static_cast<int>(i);
  return -1;
}

std::string SpriteSlices::uniqueName(const std::string& prefix) const {
  for (int n = 0;; ++n) {
    const std::string s = prefix + "_" + std::to_string(n);
    if (find(s) < 0) return s;
  }
}

int SpriteSlices::addRect(const std::string& name, int x, int y, int w, int h) {
  if (w < 1 || h < 1) return -1;
  SpriteRect r;
  r.name = (name.empty() || find(name) >= 0) ? uniqueName("sprite") : name;
  r.x = x; r.y = y; r.w = w; r.h = h;
  rects.push_back(r);
  return static_cast<int>(rects.size()) - 1;
}

bool SpriteSlices::removeRect(int index) {
  if (index < 0 || index >= static_cast<int>(rects.size())) return false;
  rects.erase(rects.begin() + index);
  return true;
}

bool SpriteSlices::renameRect(int index, const std::string& name) {
  if (index < 0 || index >= static_cast<int>(rects.size()) || name.empty()) return false;
  const int other = find(name);
  if (other >= 0 && other != index) return false;
  rects[index].name = name;
  return true;
}

bool SpriteSlices::setRect(int index, int x, int y, int w, int h) {
  if (index < 0 || index >= static_cast<int>(rects.size()) || w < 1 || h < 1) return false;
  SpriteRect& r = rects[index];
  r.x = x; r.y = y; r.w = w; r.h = h;
  return true;
}

bool SpriteSlices::setPivot(int index, int pivotX, int pivotY) {
  if (index < 0 || index >= static_cast<int>(rects.size())) return false;
  rects[index].pivotX = pivotX;
  rects[index].pivotY = pivotY;
  return true;
}

bool SpriteSlices::setPivot(int index, PivotPreset p) {
  if (index < 0 || index >= static_cast<int>(rects.size())) return false;
  const SpriteRect& r = rects[index];
  const int col = static_cast<int>(p) % 3, row = static_cast<int>(p) / 3;  // 0 left/top, 1 middle, 2 right/bottom
  return setPivot(index, col == 0 ? 0 : col == 1 ? r.w / 2 : r.w, row == 0 ? 0 : row == 1 ? r.h / 2 : r.h);
}

bool SpriteSlices::validate(int texW, int texH, std::string& err) const {
  for (size_t i = 0; i < rects.size(); ++i) {
    const SpriteRect& r = rects[i];
    const std::string who = "slice " + std::to_string(i) + " (" + r.name + ")";
    if (r.name.empty()) { err = "slice " + std::to_string(i) + " has no name"; return false; }
    if (find(r.name) != static_cast<int>(i)) { err = who + ": the name is used twice"; return false; }
    if (r.w < 1 || r.h < 1) { err = who + ": width and height must be at least 1"; return false; }
    if (r.x < 0 || r.y < 0 || r.x + r.w > texW || r.y + r.h > texH) {
      err = who + ": outside the texture (" + std::to_string(texW) + "x" + std::to_string(texH) + ")";
      return false;
    }
  }
  return true;
}

// ---- SpriteRenderer -----------------------------------------------------------------------------------------------
SliceFields sliceFields(const SpriteRect& r) { return SliceFields{r.x, r.y, r.w, r.h, r.pivotX, r.pivotY}; }

void applySlice(SpriteRenderer& sr, const SpriteRect& r) {
  sr.srcX = r.x;
  sr.srcY = r.y;
  sr.srcW = r.w;
  sr.srcH = r.h;
  sr.pivotX = r.pivotX;
  sr.pivotY = r.pivotY;
}

bool applySlice(SpriteRenderer& sr, const SpriteSlices& slices, const std::string& name) {
  const int i = slices.find(name);
  if (i < 0) return false;
  applySlice(sr, slices.rects[i]);
  return true;
}

// ---- asset database -----------------------------------------------------------------------------------------------
bool loadSlices(const AssetDatabase& db, const std::string& guid, SpriteSlices& out, std::string& err) {
  err.clear();
  if (!db.findGuid(guid)) { err = "unknown asset " + guid; return false; }
  out = SpriteSlices();
  const json j = db.sprites(guid);
  if (j.is_null()) return true;
  return SpriteSlices::fromJson(j, out, err);
}

bool saveSlices(AssetDatabase& db, const std::string& guid, const SpriteSlices& slices, std::string& err) {
  err.clear();
  if (!db.findGuid(guid)) { err = "unknown asset " + guid; return false; }
  const bool ok = slices.empty() && slices.mode == SliceMode::Manual ? db.setSprites(guid, json()) : db.setSprites(guid, slices.toJson());
  if (!ok) err = "cannot write the .meta of " + db.describeGuid(guid);
  return ok;
}

}  // namespace editor
}  // namespace rensga
