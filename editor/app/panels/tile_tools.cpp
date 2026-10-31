// F4.08 part 2: Tile Palette window, tile painting in the 2D Scene view, tilemap edits shared with the undo stack, the
// "tile:" --ui-event verbs. See tile_tools.h.
#include "panels/tile_tools.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "panels/asset_ui.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

TileTools& tileTools() {
  static TileTools* t = new TileTools();  // leaked on purpose, like assetUi()
  return *t;
}

const char* tileBrushName(TileBrush b) {
  switch (b) {
    case TileBrush::Paint: return "paint";
    case TileBrush::Erase: return "erase";
    case TileBrush::Rect: return "rect";
    case TileBrush::Fill: return "fill";
    case TileBrush::Rule: return "rule";
  }
  return "paint";
}

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t at = 0;
  for (;;) {
    const size_t e = s.find(sep, at);
    out.push_back(s.substr(at, e == std::string::npos ? std::string::npos : e - at));
    if (e == std::string::npos) break;
    at = e + 1;
  }
  return out;
}

long long fileStamp(const fs::path& p) {
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

AssetDatabase& db() { return assetUi().sync.db(); }

std::string relOfGuid(const std::string& guid) {
  const AssetEntry* e = db().findGuid(guid);
  return e ? e->rel : std::string("(none)");
}

bool endsWith(const std::string& s, const char* suffix) {
  const std::string x(suffix);
  return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

bool isRuleTileAsset(const AssetEntry& e) { return endsWith(e.rel, ".ruletile.json"); }

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::fflush(stdout);
}

}  // namespace

// ---- tilemap edits ------------------------------------------------------------------------------------------------
void bindScene2dResources(App& a) {
  static bool bound = false;
  if (bound) return;
  bound = true;
  a.res2d.setResolver([](const std::string& g) {
    const AssetEntry* e = assetUi().sync.db().findGuid(g);
    return e ? e->path : fs::path();
  });
  a.res2d.setLiveTilemaps([](const std::string& g) { return tileEditLoaded(g); });
}

const TilemapEdit* tileEditLoaded(const std::string& guid) {
  TileTools& tt = tileTools();
  auto it = tt.maps.find(guid);
  return it == tt.maps.end() ? nullptr : it->second.get();
}

TilemapEdit* tileEdit(App& a, const std::string& guid) {
  TileTools& tt = tileTools();
  if (guid.empty()) return nullptr;
  auto it = tt.maps.find(guid);
  if (it == tt.maps.end()) {
    if (tt.bad.count(guid)) return nullptr;
    const AssetEntry* e = db().findGuid(guid);
    if (!e || e->kind != AssetKind::Tilemap) {
      tt.bad.insert(guid);
      EditorLog::instance().warn("tilemap: no tilemap asset with GUID " + guid);
      return nullptr;
    }
    auto m = std::make_unique<TilemapEdit>();
    std::string err;
    if (!m->load(e->path, err)) {
      tt.bad.insert(guid);
      tt.message = e->rel + ": " + err;
      EditorLog::instance().error("tilemap: cannot read " + e->rel + ": " + err);
      return nullptr;
    }
    for (const std::string& w : m->warnings()) EditorLog::instance().warn("tilemap " + e->rel + ": " + w);
    tt.stamps[guid] = fileStamp(e->path);
    it = tt.maps.emplace(guid, std::move(m)).first;
  }
  it->second->setUndoStack(&a.doc.undoStack());
  return it->second.get();
}

std::string tilemapGuidOfObject(App& a, const std::string& objectGuid) {
  const SceneObject* o = a.doc.find(objectGuid);
  if (!o) return std::string();
  for (const SceneComponent& c : o->components)
    if (c.type == "Tilemap") {
      auto it = c.fields.find("map");
      if (it != c.fields.end() && it->is_string()) return it->get<std::string>();
    }
  return std::string();
}

int saveDirtyTilemaps(App& a) {
  (void)a;
  TileTools& tt = tileTools();
  int n = 0;
  for (auto& kv : tt.maps) {
    TilemapEdit& m = *kv.second;
    if (!m.dirty()) continue;
    std::string err;
    if (m.save(err)) {
      ++n;
      tt.stamps[kv.first] = fileStamp(m.path());
      EditorLog::instance().info("tilemap saved: " + relOfGuid(kv.first));
      say("tile: saved %s\n", relOfGuid(kv.first).c_str());
    } else {
      EditorLog::instance().error("tilemap save failed: " + relOfGuid(kv.first) + ": " + err);
    }
  }
  return n;
}

void tileToolsReset() {
  TileTools& tt = tileTools();
  tt.maps.clear();
  tt.stamps.clear();
  tt.bad.clear();
  tt.activeGuid.clear();
  tt.lastSelObject.clear();
  tt.ruleGuid.clear();
  tt.ruleOk = false;
  tt.stroking = false;
  tt.sim = TileTools::Sim();
  tt.message.clear();
}

static void endOpenStroke(TileTools& tt) {
  if (!tt.stroking) return;
  tt.stroking = false;
  auto it = tt.maps.find(tt.strokeMap);
  if (it != tt.maps.end()) it->second->endStroke();
}

void tileToolsTick(App& a) {
  TileTools& tt = tileTools();
  if (!a.loaded) return;
  bindScene2dResources(a);
  if (a.doc.selected() != tt.lastSelObject) {  // the palette follows the selection onto a Tilemap object
    tt.lastSelObject = a.doc.selected();
    const std::string g = tilemapGuidOfObject(a, tt.lastSelObject);
    if (!g.empty()) tt.activeGuid = g;
  }
  if (tt.stroking && !tt.sim.active && !ImGui::GetIO().MouseDown[0]) endOpenStroke(tt);
  // Files changed outside the editor (git, another tool) reload while nothing in the undo history points at the edit.
  const double now = ImGui::GetTime();
  if (now - tt.stampCheck > 1.0) {
    tt.stampCheck = now;
    if (a.doc.undoDepth() == 0 && !a.doc.undoStack().canRedo() && !tt.stroking) {
      for (auto& kv : tt.maps) {
        TilemapEdit& m = *kv.second;
        if (m.dirty() || m.path().empty()) continue;
        const long long st = fileStamp(m.path());
        if (st == tt.stamps[kv.first]) continue;
        std::string err;
        if (m.load(m.path(), err)) EditorLog::instance().info("tilemap reloaded: " + relOfGuid(kv.first));
        tt.stamps[kv.first] = st;
      }
    }
  }
}

void openTilePalette(App& a, const std::string& guid, bool newDialog) {
  TileTools& tt = tileTools();
  (void)a;
  tt.showPalette = true;
  if (!guid.empty()) tt.activeGuid = guid;
  if (newDialog) tt.openNew = true;
  ImGui::SetWindowFocus("Tile Palette");
}

static bool assignToSelection(App& a, const std::string& tilemapGuid) {
  const std::string sel = a.doc.selected();
  if (sel.empty()) return false;
  std::vector<ComponentRef> refs = sameTypeAcross(a.doc, {sel}, "Tilemap");
  if (refs.empty()) return false;
  const nlohmann::json cur = fieldValue(a.doc, refs[0], "map");
  if (cur.is_string() && !cur.get<std::string>().empty() && cur.get<std::string>() != "00000000-0000-0000-0000-000000000000") return false;
  return editFields(a.doc, refs, "map", tilemapGuid).ok;
}

std::string newTilemapAsset(App& a, const std::string& name, const std::string& tilesetGuid, int tileSize, int w, int h) {
  TileTools& tt = tileTools();
  EditorLog& log = EditorLog::instance();
  const AssetEntry* ts = db().findGuid(tilesetGuid);
  if (!ts || ts->kind != AssetKind::Texture) { tt.message = "pick a tileset texture first"; return std::string(); }
  if (!AssetDatabase::validName(name)) { tt.message = "'" + name + "' is not a valid file name"; return std::string(); }
  fs::path file = ts->path.parent_path() / (name + ".tilemap.json");
  for (int k = 2; fs::exists(file); ++k) file = ts->path.parent_path() / (name + "_" + std::to_string(k) + ".tilemap.json");
  const fs::path relFile = fs::path(ts->rel).parent_path() / file.filename();  // `ts` points into the database, which the scan below rebuilds
  TilemapEdit tmp;
  tmp.create(tilesetGuid, tileSize, w, h);
  std::string err;
  if (!tmp.saveAs(file, err)) { tt.message = "cannot write " + file.string() + ": " + err; log.error("tilemap: " + tt.message); return std::string(); }
  assetUi().sync.syncNow(false);  // writes the .meta (and the GUID)
  const AssetEntry* e = db().findPath(relFile.generic_string());
  if (!e) { tt.message = "the new tilemap was not found by the asset scan"; return std::string(); }
  const std::string guid = e->guid;
  a.assetsStamp = -100;
  tt.activeGuid = guid;
  tt.message.clear();
  log.info("tilemap created: " + e->rel + " (" + std::to_string(w) + "x" + std::to_string(h) + " tiles of " + std::to_string(tileSize) + " px)");
  return guid;
}

// ---- brushes (shared by the mouse and the verbs) ---------------------------------------------------------------
namespace {

bool ruleActive(const TileTools& tt) { return tt.brush == TileBrush::Rule && tt.ruleOk; }

int stampCell(TileTools& tt, TilemapEdit& e, int x, int y, bool erase) {
  if (erase) return ruleActive(tt) ? e.eraseRule(x, y, tt.rule) : e.erase(x, y);
  return ruleActive(tt) ? e.paintRule(x, y, tt.rule) : e.paint(x, y, static_cast<uint16_t>(tt.tile));
}

int stampRect(TileTools& tt, TilemapEdit& e, int x0, int y0, int x1, int y1, bool erase) {
  if (erase) return ruleActive(tt) ? e.eraseRuleRect(x0, y0, x1, y1, tt.rule) : e.eraseRect(x0, y0, x1, y1);
  return ruleActive(tt) ? e.paintRuleRect(x0, y0, x1, y1, tt.rule) : e.paintRect(x0, y0, x1, y1, static_cast<uint16_t>(tt.tile));
}

int stampFill(TileTools& tt, TilemapEdit& e, int x, int y, bool erase) {
  if (erase) return e.fill(x, y, kTileEmpty);
  return ruleActive(tt) ? e.fillRule(x, y, tt.rule) : e.fill(x, y, static_cast<uint16_t>(tt.tile));
}

const char* strokeName(const TileTools& tt, bool erase) {
  if (erase) return "Erase Tiles";
  switch (tt.brush) {
    case TileBrush::Rect: return "Paint Tile Rectangle";
    case TileBrush::Fill: return "Fill Tiles";
    case TileBrush::Rule: return "Paint Rule Tiles";
    default: return "Paint Tiles";
  }
}

// Cells on the line from (x0, y0) to (x1, y1), both ends included.
void line(int x0, int y0, int x1, int y1, std::vector<std::pair<int, int>>& out) {
  const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    out.emplace_back(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
    if (out.size() > 20000) break;
  }
}

}  // namespace

// ---- Scene view ----------------------------------------------------------------------------------------------------
static const TileLayer2D* findTarget(App& a, const Scene2D& s2d) {
  TileTools& tt = tileTools();
  const std::string sel = a.doc.selected();
  for (const TileLayer2D& l : s2d.layers)
    if (!l.map.empty() && l.guid == sel && (tt.activeGuid.empty() || l.map == tt.activeGuid)) return &l;
  for (const TileLayer2D& l : s2d.layers)
    if (!l.map.empty() && l.map == tt.activeGuid) return &l;
  return nullptr;
}

bool tilePaintingActive(App& a, const Scene2D& s2d) {
  TileTools& tt = tileTools();
  if (!tt.showPalette || !tt.paintInScene || !a.sv.mode2d || a.doc.playing() || a.session.active()) return false;
  return findTarget(a, s2d) != nullptr;
}

static ImVec2 toScreen(const TilePaintView& v, const M4& model, float lx, float ly) {
  const float p[3] = {lx, ly, 0};
  float w[3], c[3];
  m4Point(model, p, w);
  m4Point(m4Mul(v.proj, v.view), w, c);
  return ImVec2(v.p0.x + (c[0] * 0.5f + 0.5f) * v.size.x, v.p0.y + (0.5f - c[1] * 0.5f) * v.size.y);
}

void tileSceneInput(App& a, const Scene2D& s2d, const TilePaintView& v) {
  TileTools& tt = tileTools();
  ImGuiIO& io = ImGui::GetIO();
  tt.hoverValid = false;
  const TileLayer2D* layer = tilePaintingActive(a, s2d) ? findTarget(a, s2d) : nullptr;
  TilemapEdit* e = layer ? tileEdit(a, layer->map) : nullptr;
  if (!layer || !e) { endOpenStroke(tt); return; }
  const int ts = e->tileSize();
  const M4 inv = m4Invert(layer->model);
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // the cell under the mouse
  float w[2];
  a.sv.cam2.screenToWorld(io.MousePos.x - v.p0.x, io.MousePos.y - v.p0.y, v.size.x, v.size.y, w);
  const float wp[3] = {w[0], w[1], 0};
  float lp[3];
  m4Point(inv, wp, lp);
  const int cx = static_cast<int>(std::floor(lp[0] / static_cast<float>(ts))), cy = static_cast<int>(std::floor(lp[1] / static_cast<float>(ts)));
  tt.hoverValid = v.hot;
  tt.hoverX = cx;
  tt.hoverY = cy;
  const bool erasing = tt.brush == TileBrush::Erase || io.KeyShift;

  if (!tt.stroking) {
    if (v.hot && io.MouseClicked[0] && !io.KeyAlt && !io.KeyCtrl) {
      tt.strokeMap = layer->map;
      tt.strokeErase = erasing;
      tt.strokeRect = tt.brush == TileBrush::Rect;
      tt.startX = tt.lastX = cx;
      tt.startY = tt.lastY = cy;
      if (tt.brush == TileBrush::Fill) {
        e->beginStroke(strokeName(tt, erasing));
        stampFill(tt, *e, cx, cy, erasing);
        e->endStroke();
      } else {
        tt.stroking = true;
        if (!tt.strokeRect) {
          e->beginStroke(strokeName(tt, erasing));
          stampCell(tt, *e, cx, cy, erasing);
        }
      }
    }
  } else if (tt.strokeMap != layer->map) {
    endOpenStroke(tt);
  } else {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && tt.strokeRect) {
      tt.stroking = false;  // nothing was painted yet
    } else if (io.MouseDown[0]) {
      if (!tt.strokeRect && (cx != tt.lastX || cy != tt.lastY)) {
        std::vector<std::pair<int, int>> cells;
        line(tt.lastX, tt.lastY, cx, cy, cells);
        for (size_t i = 1; i < cells.size(); ++i) stampCell(tt, *e, cells[i].first, cells[i].second, tt.strokeErase);
        tt.lastX = cx;
        tt.lastY = cy;
      }
    } else {
      if (tt.strokeRect) {
        e->beginStroke(strokeName(tt, tt.strokeErase));
        stampRect(tt, *e, tt.startX, tt.startY, cx, cy, tt.strokeErase);
      }
      e->endStroke();
      tt.stroking = false;
    }
  }

  // hover preview: the cell outline (and the tile about to be placed), the dragged rectangle
  const ImU32 outline = ImGui::GetColorU32(erasing ? brandToken("danger") : brandToken("primary"));
  auto cellQuad = [&](int x0, int y0, int x1, int y1, ImVec2 q[4]) {
    q[0] = toScreen(v, layer->model, static_cast<float>(x0 * ts), static_cast<float>(y0 * ts));
    q[1] = toScreen(v, layer->model, static_cast<float>((x1 + 1) * ts), static_cast<float>(y0 * ts));
    q[2] = toScreen(v, layer->model, static_cast<float>((x1 + 1) * ts), static_cast<float>((y1 + 1) * ts));
    q[3] = toScreen(v, layer->model, static_cast<float>(x0 * ts), static_cast<float>((y1 + 1) * ts));
  };
  ImVec2 q[4];
  if (tt.stroking && tt.strokeRect) {
    cellQuad(std::min(tt.startX, cx), std::min(tt.startY, cy), std::max(tt.startX, cx), std::max(tt.startY, cy), q);
    ImVec4 f = erasing ? brandToken("danger") : brandToken("primary");
    f.w = 0.18f;
    dl->AddConvexPolyFilled(q, 4, ImGui::GetColorU32(f));
    dl->AddPolyline(q, 4, outline, ImDrawFlags_Closed, 2.0f);
  } else if (v.hot) {
    cellQuad(cx, cy, cx, cy, q);
    if (!erasing && tt.brush != TileBrush::Rule && tt.tile >= 0 && e->inside(cx, cy)) {
      if (const Scene2DResources::Tex* tex = a.res2d.texture(e->tileset())) {
        const int cols = std::max(1, tex->w / ts);
        const float u0 = static_cast<float>((tt.tile % cols) * ts) / static_cast<float>(tex->w);
        const float v0 = static_cast<float>((tt.tile / cols) * ts) / static_cast<float>(tex->h);
        const float u1 = u0 + static_cast<float>(ts) / static_cast<float>(tex->w), v1 = v0 + static_cast<float>(ts) / static_cast<float>(tex->h);
        imageNearestBegin(dl);
        dl->AddImageQuad(static_cast<ImTextureID>(tex->gl), q[0], q[1], q[2], q[3], ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1),
                         IM_COL32(255, 255, 255, 150));
        imageNearestEnd(dl);
      }
    }
    dl->AddPolyline(q, 4, outline, ImDrawFlags_Closed, 2.0f);
  }
  // status label (words, not just colour)
  char label[200];
  std::snprintf(label, sizeof label, "Painting %s  |  brush %s%s  |  tile %d  |  cell %d,%d  |  Shift erases", relOfGuid(layer->map).c_str(),
                tileBrushName(tt.brush), ruleActive(tt) ? " (rule)" : "", tt.tile, cx, cy);
  const ImVec2 ts2 = ImGui::CalcTextSize(label);
  const ImVec2 at(v.p0.x + 10, v.p0.y + v.size.y - ts2.y - 10);
  dl->AddRectFilled(ImVec2(at.x - 6, at.y - 3), ImVec2(at.x + ts2.x + 6, at.y + ts2.y + 3), ImGui::GetColorU32(brandToken("surface")), 4);
  dl->AddText(at, ImGui::GetColorU32(brandToken("text")), label);
}

// ---- scripted mouse (--ui-event tiledrag) ---------------------------------------------------------------------------
bool tileSimHolds() { return tileTools().sim.active; }

void tileSimFrame(App& a) {
  TileTools& tt = tileTools();
  TileTools::Sim& s = tt.sim;
  if (!s.active) return;
  ImGuiIO& io = ImGui::GetIO();
  const Scene2D s2d = a.doc.loaded() ? gatherScene2D(a.doc) : Scene2D();
  const TileLayer2D* layer = findTarget(a, s2d);
  TilemapEdit* e = layer ? tileEdit(a, layer->map) : nullptr;
  if (!layer || !e || s.cells.empty()) { s.active = false; EditorLog::instance().warn("tiledrag: no tilemap to paint"); return; }
  TilePaintView v;
  v.p0 = a.sv.lastP0;
  v.size = a.sv.lastSize;
  v.view = a.sv.lastView;
  v.proj = a.sv.lastProj;
  auto where = [&](size_t i) {
    const float ts = static_cast<float>(e->tileSize());
    return toScreen(v, layer->model, (static_cast<float>(s.cells[i].first) + 0.5f) * ts, (static_cast<float>(s.cells[i].second) + 0.5f) * ts);
  };
  if (s.phase == 0) {
    const ImVec2 p = where(0);
    io.AddMousePosEvent(p.x, p.y);
  } else if (s.phase == 1) {
    io.AddMouseButtonEvent(0, true);
  } else if (static_cast<size_t>(s.phase - 1) < s.cells.size()) {
    const ImVec2 p = where(static_cast<size_t>(s.phase - 1));
    io.AddMousePosEvent(p.x, p.y);
  } else {
    io.AddMouseButtonEvent(0, false);
    s.active = false;
  }
  ++s.phase;
}

// ---- Tile Palette window ------------------------------------------------------------------------------------------------
static const char* kindWord(const TileProps& p) {
  switch (p.kind) {
    case TileKind::Solid: return "Solid";
    case TileKind::OneWay: return "One-way";
    case TileKind::Slope: return "Slope";
    default: return "None";
  }
}

static void drawNewTilemapPopup(App& a) {
  TileTools& tt = tileTools();
  if (tt.openNew) {
    tt.openNew = false;
    if (tt.newTileset.empty())
      for (const AssetEntry& e : db().entries())
        if (e.kind == AssetKind::Texture && e.rel.find("tile") != std::string::npos) { tt.newTileset = e.guid; break; }
    if (tt.newTileset.empty())
      for (const AssetEntry& e : db().entries())
        if (e.kind == AssetKind::Texture) { tt.newTileset = e.guid; break; }
    ImGui::OpenPopup("New Tilemap");
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("New Tilemap", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::SetNextItemWidth(260);
  ImGui::InputText("Name", tt.newName, sizeof tt.newName);
  ImGui::SetNextItemWidth(260);
  if (ImGui::BeginCombo("Tileset", tt.newTileset.empty() ? "(pick a texture)" : relOfGuid(tt.newTileset).c_str())) {
    for (const AssetEntry& e : db().entries())
      if (e.kind == AssetKind::Texture && ImGui::Selectable(e.rel.c_str(), e.guid == tt.newTileset)) tt.newTileset = e.guid;
    ImGui::EndCombo();
  }
  ImGui::SetNextItemWidth(120);
  ImGui::InputInt("Tile size (px)", &tt.newTileSize);
  ImGui::SetNextItemWidth(120);
  ImGui::InputInt("Width (tiles)", &tt.newW);
  ImGui::SetNextItemWidth(120);
  ImGui::InputInt("Height (tiles)", &tt.newH);
  tt.newTileSize = std::clamp(tt.newTileSize, 1, 256);
  tt.newW = std::clamp(tt.newW, 1, 512);
  tt.newH = std::clamp(tt.newH, 1, 512);
  if (!tt.message.empty()) ImGui::TextColored(brandToken("danger"), "[!] %s", tt.message.c_str());
  ImGui::BeginDisabled(tt.newTileset.empty());
  ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary"));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
  if (ImGui::Button("Create", ImVec2(96, 0))) {
    const std::string g = newTilemapAsset(a, tt.newName, tt.newTileset, tt.newTileSize, tt.newW, tt.newH);
    if (!g.empty()) {
      assignToSelection(a, g);
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::PopStyleColor(2);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(96, 0))) { tt.message.clear(); ImGui::CloseCurrentPopup(); }
  ImGui::EndPopup();
}

static void loadRuleTile(TileTools& tt, const std::string& guid) {
  tt.ruleGuid = guid;
  tt.ruleOk = false;
  tt.ruleError.clear();
  const AssetEntry* e = db().findGuid(guid);
  if (!e) { tt.ruleError = "no such asset"; return; }
  tt.ruleOk = tt.rule.load(e->path, tt.ruleError);
  if (tt.ruleOk) tt.tile = tt.rule.defaultTile;
}

static void drawBrushBar(TileTools& tt) {
  struct B { TileBrush b; const char* label; const char* tip; };
  static const B kBrushes[] = {{TileBrush::Paint, "Paint", "Paint the selected tile (drag)"},
                               {TileBrush::Erase, "Erase", "Erase tiles (drag)"},
                               {TileBrush::Rect, "Rect", "Drag a rectangle of the selected tile"},
                               {TileBrush::Fill, "Fill", "Flood fill the area under the cursor"},
                               {TileBrush::Rule, "Rule", "Paint with a rule tile: the tile follows its neighbours"}};
  for (size_t i = 0; i < 5; ++i) {
    const bool on = tt.brush == kBrushes[i].b;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? brandToken("primary") : brandToken("surface"));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? brandToken("on-primary") : brandToken("text-2"));
    if (ImGui::Button(kBrushes[i].label)) tt.brush = kBrushes[i].b;
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kBrushes[i].tip);
    if (i < 4) ImGui::SameLine();
  }
}

static void drawTilesetView(App& a, TileTools& tt, TilemapEdit& e) {
  const Scene2DResources::Tex* tex = a.res2d.texture(e.tileset());
  if (!tex) { ImGui::TextDisabled("Tileset texture %s is not available", relOfGuid(e.tileset()).c_str()); return; }
  const int ts = e.tileSize();
  if (ts <= 0) return;
  const int cols = std::max(1, tex->w / ts), rows = std::max(1, tex->h / ts);
  ImGui::SetNextItemWidth(120);
  ImGui::SliderInt("Zoom", &tt.zoom, 1, 8, "%dx");
  const float z = static_cast<float>(tt.zoom);
  const ImVec2 size(static_cast<float>(tex->w) * z, static_cast<float>(tex->h) * z);
  const float h = std::max(120.0f, ImGui::GetContentRegionAvail().y - 150.0f);
  ImGui::BeginChild("##tileset", ImVec2(0, std::min(h, size.y + 20)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), ImGui::GetColorU32(brandToken("surface-2")));
  imageNearestBegin(dl);
  dl->AddImage(static_cast<ImTextureID>(tex->gl), p0, ImVec2(p0.x + size.x, p0.y + size.y));
  imageNearestEnd(dl);
  ImGui::InvisibleButton("##ts", size);
  const bool hovered = ImGui::IsItemHovered();
  ImVec4 gridCol = brandToken("border-strong");
  gridCol.w = 0.55f;
  for (int c = 0; c <= cols; ++c) dl->AddLine(ImVec2(p0.x + c * ts * z, p0.y), ImVec2(p0.x + c * ts * z, p0.y + rows * ts * z), ImGui::GetColorU32(gridCol));
  for (int r = 0; r <= rows; ++r) dl->AddLine(ImVec2(p0.x, p0.y + r * ts * z), ImVec2(p0.x + cols * ts * z, p0.y + r * ts * z), ImGui::GetColorU32(gridCol));
  // flags: a tinted cell and a letter (colour never carries the meaning alone)
  for (const auto& kv : e.flaggedTiles()) {
    const int id = kv.first;
    if (id >= cols * rows) continue;
    const ImVec2 a0(p0.x + (id % cols) * ts * z, p0.y + (id / cols) * ts * z), a1(a0.x + ts * z, a0.y + ts * z);
    ImVec4 c = kv.second.kind == TileKind::Solid ? brandToken("danger") : kv.second.kind == TileKind::OneWay ? brandToken("warn") : brandToken("info");
    c.w = 0.32f;
    dl->AddRectFilled(a0, a1, ImGui::GetColorU32(c));
    const char* word = kv.second.kind == TileKind::Solid ? "S" : kv.second.kind == TileKind::OneWay ? "O" : kv.second.slope.c_str();
    dl->AddText(ImVec2(a0.x + 2, a0.y + 1), ImGui::GetColorU32(brandToken("text")), word);
  }
  const int sel = tt.tile;
  if (sel >= 0 && sel < cols * rows) {
    const ImVec2 a0(p0.x + (sel % cols) * ts * z, p0.y + (sel / cols) * ts * z);
    dl->AddRect(a0, ImVec2(a0.x + ts * z, a0.y + ts * z), ImGui::GetColorU32(brandToken("primary")), 0, 0, 2.5f);
  }
  if (hovered) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    const int c = static_cast<int>((m.x - p0.x) / (ts * z)), r = static_cast<int>((m.y - p0.y) / (ts * z));
    if (c >= 0 && r >= 0 && c < cols && r < rows) {
      const int id = r * cols + c;
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) tt.tile = id;
      const TileProps pr = e.tileProps(static_cast<uint16_t>(id));
      ImGui::SetTooltip("Tile %d  (column %d, row %d)  %s%s%s", id, c, r, kindWord(pr), pr.slope.empty() ? "" : " ", pr.slope.c_str());
    }
  }
  ImGui::EndChild();
}

static void drawFlagEditor(TileTools& tt, TilemapEdit& e) {
  const uint16_t id = static_cast<uint16_t>(tt.tile);
  TileProps cur = e.tileProps(id);
  ImGui::SeparatorText("Tile flags");
  ImGui::Text("Tile %d: %s%s%s", tt.tile, kindWord(cur), cur.slope.empty() ? "" : " ", cur.slope.c_str());
  TileProps next = cur;
  bool changed = false;
  static const struct { TileKind k; const char* label; } kKinds[] = {
      {TileKind::None, "None"}, {TileKind::Solid, "Solid"}, {TileKind::OneWay, "One-way"}, {TileKind::Slope, "Slope"}};
  for (size_t i = 0; i < 4; ++i) {
    if (ImGui::RadioButton(kKinds[i].label, cur.kind == kKinds[i].k)) {
      next.kind = kKinds[i].k;
      if (next.kind == TileKind::Slope && next.slope.empty()) next.slope = kSlopeNames[0];
      if (next.kind != TileKind::Slope) next.slope.clear();
      changed = true;
    }
    if (i < 3) ImGui::SameLine();
  }
  if (cur.kind == TileKind::Slope) {
    ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo("Slope", cur.slope.c_str())) {
      for (const char* n : kSlopeNames)
        if (ImGui::Selectable(n, cur.slope == n)) { next.slope = n; changed = true; }
      ImGui::EndCombo();
    }
  }
  if (changed) e.setTileProps(id, next);
}

void drawTilePalette(App& a) {
  TileTools& tt = tileTools();
  if (!a.loaded || !tt.showPalette) return;
  ImGui::SetNextWindowSize(ImVec2(400, 640), ImGuiCond_FirstUseEver);
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 410, vp->WorkPos.y + 40), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Tile Palette", &tt.showPalette)) { ImGui::End(); return; }
  bindScene2dResources(a);

  // which tilemap
  const std::string curRel = tt.activeGuid.empty() ? std::string("(none)") : relOfGuid(tt.activeGuid);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120);
  if (ImGui::BeginCombo("##map", curRel.c_str())) {
    for (const AssetEntry& e : db().entries())
      if (e.kind == AssetKind::Tilemap && ImGui::Selectable(e.rel.c_str(), e.guid == tt.activeGuid)) tt.activeGuid = e.guid;
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("New Tilemap...")) tt.openNew = true;
  drawNewTilemapPopup(a);

  TilemapEdit* e = tileEdit(a, tt.activeGuid);
  if (!e) {
    ImGui::Spacing();
    ImGui::TextWrapped("Select an object with a Tilemap component, pick a tilemap above, or create one from a tileset texture.");
    if (!tt.message.empty()) ImGui::TextColored(brandToken("danger"), "[!] %s", tt.message.c_str());
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("%d x %d tiles of %d px  -  tileset %s", e->width(), e->height(), e->tileSize(), relOfGuid(e->tileset()).c_str());
  if (e->dirty()) {
    ImGui::TextColored(brandToken("warn"), "[*] Modified, not saved");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save")) { std::string err; if (e->save(err)) tt.stamps[tt.activeGuid] = fileStamp(e->path()); else tt.message = err; }
  } else {
    ImGui::TextColored(brandToken("ok"), "Saved");
  }
  // is some object painting this map?
  bool shown = false;
  for (const SceneObject& o : a.doc.scene().objects)
    if (tilemapGuidOfObject(a, o.guid) == tt.activeGuid) shown = true;
  if (!shown) ImGui::TextColored(brandToken("warn"), "[!] No object in this scene uses this tilemap, so there is nothing to paint on.");

  ImGui::Separator();
  drawBrushBar(tt);
  ImGui::Checkbox("Paint in Scene view (2D mode)", &tt.paintInScene);
  if (tt.brush == TileBrush::Rule) {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##rule", tt.ruleGuid.empty() ? "(pick a *.ruletile.json)" : relOfGuid(tt.ruleGuid).c_str())) {
      for (const AssetEntry& r : db().entries())
        if (isRuleTileAsset(r) && ImGui::Selectable(r.rel.c_str(), r.guid == tt.ruleGuid)) loadRuleTile(tt, r.guid);
      ImGui::EndCombo();
    }
    if (!tt.ruleOk) ImGui::TextColored(brandToken("warn"), "[!] %s", tt.ruleError.empty() ? "No rule tile loaded: Rule paints nothing" : tt.ruleError.c_str());
    else ImGui::TextDisabled("Rule tile: %d neighbours, %zu rules, default tile %d", tt.rule.neighbours, tt.rule.rules.size(), tt.rule.defaultTile);
  }
  if (!tt.paintInScene) ImGui::TextDisabled("Painting is off: the Scene view selects objects.");
  else if (!a.sv.mode2d) ImGui::TextDisabled("Switch the Scene view to 2D mode to paint.");
  else ImGui::TextDisabled("Drag in the Scene view to paint. Shift erases, Ctrl+click selects an object.");
  ImGui::Spacing();
  drawTilesetView(a, tt, *e);
  drawFlagEditor(tt, *e);
  if (!tt.message.empty()) ImGui::TextColored(brandToken("danger"), "[!] %s", tt.message.c_str());
  ImGui::End();
}

// ---- --ui-event verbs --------------------------------------------------------------------------------------------------
static bool parseKind(const std::string& v, TileProps& out) {
  out = TileProps();
  if (v == "none") return true;
  if (v == "solid") { out.kind = TileKind::Solid; return true; }
  if (v == "oneway" || v == "one-way") { out.kind = TileKind::OneWay; return true; }
  if (v.rfind("slope", 0) == 0 && isSlopeName(v.substr(5))) { out.kind = TileKind::Slope; out.slope = v.substr(5); return true; }
  return false;
}

bool tileUiHook(App& a, const std::string& act, const std::string& arg) {
  TileTools& tt = tileTools();
  EditorLog& log = EditorLog::instance();
  auto active = [&]() { return tileEdit(a, tt.activeGuid); };
  auto needMap = [&]() -> TilemapEdit* {
    TilemapEdit* e = active();
    if (!e) say("tile: no tilemap is open (use palette:<tilemap asset path>)\n");
    return e;
  };
  if (act == "palette") {  // palette:<tilemap asset path>
    assetUiTick(a);
    const AssetEntry* ent = db().findPath(arg);
    if (!ent || ent->kind != AssetKind::Tilemap) { log.warn("palette: not a tilemap asset: " + arg); say("tile: palette not found: %s\n", arg.c_str()); return true; }
    openTilePalette(a, ent->guid);
    TilemapEdit* e = active();
    if (e) say("tile: palette %s %dx%d tileSize=%d tileset=%s dirty=%d\n", ent->rel.c_str(), e->width(), e->height(), e->tileSize(), relOfGuid(e->tileset()).c_str(), e->dirty() ? 1 : 0);
    return true;
  }
  if (act == "brush") {
    static const TileBrush all[] = {TileBrush::Paint, TileBrush::Erase, TileBrush::Rect, TileBrush::Fill, TileBrush::Rule};
    for (TileBrush b : all)
      if (arg == tileBrushName(b)) { tt.brush = b; say("tile: brush %s\n", arg.c_str()); return true; }
    say("tile: unknown brush %s (paint|erase|rect|fill|rule)\n", arg.c_str());
    return true;
  }
  if (act == "tile") {
    tt.tile = std::max(0, std::atoi(arg.c_str()));
    say("tile: tile %d\n", tt.tile);
    return true;
  }
  if (act == "ruletile") {
    assetUiTick(a);
    const AssetEntry* ent = db().findPath(arg);
    if (!ent) { say("tile: ruletile not found: %s\n", arg.c_str()); return true; }
    loadRuleTile(tt, ent->guid);
    if (tt.ruleOk) say("tile: ruletile %s neighbours=%d rules=%zu default=%d\n", ent->rel.c_str(), tt.rule.neighbours, tt.rule.rules.size(), tt.rule.defaultTile);
    else say("tile: ruletile %s failed: %s\n", ent->rel.c_str(), tt.ruleError.c_str());
    return true;
  }
  if (act == "painttile") {  // painttile:x,y[;x,y...]: one stroke with the current brush (Shift/Erase brush erase)
    TilemapEdit* e = needMap();
    if (!e) return true;
    int writes = 0, cells = 0;
    const bool erase = tt.brush == TileBrush::Erase;
    e->beginStroke(strokeName(tt, erase));
    for (const std::string& c : split(arg, ';')) {
      int x = 0, y = 0;
      if (std::sscanf(c.c_str(), "%d,%d", &x, &y) != 2) continue;
      ++cells;
      writes += stampCell(tt, *e, x, y, erase);
    }
    e->endStroke();
    say("tile: painttile brush=%s cells=%d writes=%d undo=%zu dirty=%d\n", tileBrushName(tt.brush), cells, writes, a.doc.undoDepth(), e->dirty() ? 1 : 0);
    return true;
  }
  if (act == "paintrect") {
    TilemapEdit* e = needMap();
    int x0, y0, x1, y1;
    if (!e) return true;
    if (std::sscanf(arg.c_str(), "%d,%d,%d,%d", &x0, &y0, &x1, &y1) != 4) { say("tile: paintrect:x0,y0,x1,y1\n"); return true; }
    const bool erase = tt.brush == TileBrush::Erase;
    e->beginStroke(strokeName(tt, erase));
    const int writes = stampRect(tt, *e, x0, y0, x1, y1, erase);
    e->endStroke();
    say("tile: paintrect brush=%s (%d,%d)-(%d,%d) writes=%d undo=%zu\n", tileBrushName(tt.brush), x0, y0, x1, y1, writes, a.doc.undoDepth());
    return true;
  }
  if (act == "filltile") {
    TilemapEdit* e = needMap();
    int x, y;
    if (!e) return true;
    if (std::sscanf(arg.c_str(), "%d,%d", &x, &y) != 2) { say("tile: filltile:x,y\n"); return true; }
    const bool erase = tt.brush == TileBrush::Erase;
    e->beginStroke(strokeName(tt, erase));
    const int writes = stampFill(tt, *e, x, y, erase);
    e->endStroke();
    say("tile: filltile (%d,%d) writes=%d undo=%zu\n", x, y, writes, a.doc.undoDepth());
    return true;
  }
  if (act == "tileflag") {  // tileflag:<id>=<solid|oneway|slope45L|...|none>
    TilemapEdit* e = needMap();
    const size_t eq = arg.find('=');
    TileProps p;
    if (!e) return true;
    if (eq == std::string::npos || !parseKind(arg.substr(eq + 1), p)) { say("tile: tileflag:<id>=<solid|oneway|slope45L|slope45R|slope22L_lo|...|none>\n"); return true; }
    const int id = std::atoi(arg.c_str());
    const bool ok = e->setTileProps(static_cast<uint16_t>(id), p);
    say("tile: flag %d=%s %s undo=%zu\n", id, arg.substr(eq + 1).c_str(), ok ? "ok" : "refused", a.doc.undoDepth());
    return true;
  }
  if (act == "savetiles") {
    const int n = saveDirtyTilemaps(a);
    say("tile: savetiles wrote %d file(s)\n", n);
    return true;
  }
  if (act == "dumptiles") {  // dumptiles:x0,y0,x1,y1
    TilemapEdit* e = needMap();
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!e) return true;
    if (arg.empty()) { x1 = e->width() - 1; y1 = e->height() - 1; }
    else if (std::sscanf(arg.c_str(), "%d,%d,%d,%d", &x0, &y0, &x1, &y1) != 4) { say("tile: dumptiles:x0,y0,x1,y1\n"); return true; }
    size_t filled = 0;
    for (uint16_t t : e->tiles()) if (t != kTileEmpty) ++filled;
    say("tile: dump %s (%d,%d)-(%d,%d) size=%dx%d filled=%zu dirty=%d revision=%u undo=%zu\n", relOfGuid(tt.activeGuid).c_str(), x0, y0, x1, y1, e->width(),
        e->height(), filled, e->dirty() ? 1 : 0, e->revision(), a.doc.undoDepth());
    for (int y = y0; y <= y1; ++y) {
      std::string row;
      for (int x = x0; x <= x1; ++x) {
        const uint16_t t = e->at(x, y);
        row += (x == x0 ? "" : " ") + (t == kTileEmpty ? std::string(".") : std::to_string(t));
      }
      say("tile: row %d: %s\n", y, row.c_str());
    }
    for (const auto& kv : e->flaggedTiles()) say("tile: flagged %d=%s%s\n", kv.first, kindWord(kv.second), kv.second.slope.c_str());
    return true;
  }
  if (act == "newtilemap") {  // newtilemap:<name>|<tileset texture path>|<tile size>|<w>x<h>: assigns it to the selected Tilemap component
    const std::vector<std::string> p = split(arg, '|');
    assetUiTick(a);
    const AssetEntry* ts = p.size() > 1 ? db().findPath(p[1]) : nullptr;
    int w = 40, h = 15;
    if (p.size() > 3) std::sscanf(p[3].c_str(), "%dx%d", &w, &h);
    if (!ts) { say("tile: newtilemap: tileset texture not found\n"); return true; }
    const std::string g = newTilemapAsset(a, p[0], ts->guid, p.size() > 2 ? std::max(1, std::atoi(p[2].c_str())) : 16, w, h);
    if (g.empty()) { say("tile: newtilemap failed: %s\n", tt.message.c_str()); return true; }
    const bool assigned = assignToSelection(a, g);
    tt.showPalette = true;
    tileEdit(a, g);
    say("tile: newtilemap %s guid=%s %dx%d assigned=%d\n", relOfGuid(g).c_str(), g.c_str(), w, h, assigned ? 1 : 0);
    return true;
  }
  if (act == "tiledrag") {  // tiledrag:x0,y0,x1,y1[;x,y...]: a real mouse stroke through the Scene view over those cells
    tt.sim = TileTools::Sim();
    for (const std::string& c : split(arg, ';')) {
      std::vector<int> v;
      size_t at = 0;
      while (at <= c.size()) {
        size_t e2 = c.find(',', at);
        v.push_back(std::atoi(c.substr(at, e2 == std::string::npos ? std::string::npos : e2 - at).c_str()));
        if (e2 == std::string::npos) break;
        at = e2 + 1;
      }
      for (size_t i = 0; i + 1 < v.size(); i += 2) tt.sim.cells.emplace_back(v[i], v[i + 1]);
    }
    tt.sim.active = !tt.sim.cells.empty();
    say("tile: tiledrag %zu point(s)\n", tt.sim.cells.size());
    return true;
  }
  if (act == "tilepaint") {  // tilepaint:on|off: LMB in the Scene view paints / selects
    tt.paintInScene = arg != "off";
    return true;
  }
  if (act == "closetools") {  // closes the Tile Palette and the Sprite Editor windows (clean Game view screenshots)
    tt.showPalette = false;
    spriteUiHook(a, "spriteclose", "");
    return true;
  }
  if (act == "undoname") {  // undoname: the top of the shared undo history, to show the order scene and tile edits were made in
    UndoStack& u = a.doc.undoStack();
    say("tile: undo-top='%s' depth=%zu redo-top='%s' redo-depth=%zu\n", u.undoName(), u.depth(), u.redoName(), u.redoDepth());
    return true;
  }
  if (act == "tilehover") {
    say("tile: hover valid=%d cell=%d,%d stroking=%d\n", tt.hoverValid ? 1 : 0, tt.hoverX, tt.hoverY, tt.stroking ? 1 : 0);
    return true;
  }
  return false;
}

}  // namespace editor
}  // namespace rensga
