// F4.08 part 2: the Sprite Editor window (Window > Sprite Editor, double-click a texture in the Project window, or the
// button in the texture's inspector). Edits a working copy of the texture's slices (editor/core SpriteSlices); Apply writes
// them to the .meta ("sprites" key), Revert reads them back. The window has its own undo history (Ctrl+Z while it is focused).
#include <algorithm>
#include "panel_registry.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "panels/asset_ui.h"
#include "panels/tile_tools.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

bool pngFileSize(const fs::path& file, int& w, int& h) {
  std::ifstream in(file, std::ios::binary);
  unsigned char b[24] = {};
  in.read(reinterpret_cast<char*>(b), sizeof b);
  static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (in.gcount() < 24 || !std::equal(sig, sig + 8, b)) return false;
  w = static_cast<int>((b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19]);
  h = static_cast<int>((b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23]);
  return w > 0 && h > 0;
}

namespace {

enum Handle { kL = 1, kR = 2, kT = 4, kB = 8 };
enum class Drag { None, NewRect, Move, Resize, Pivot };

struct SpriteEd {
  bool show = false;
  bool focused = false;              // as of the last draw
  std::string guid, rel, stem;
  int texW = 0, texH = 0;
  SpriteSlices cur, saved, prev;     // prev = cur as of the last undo bookkeeping
  std::vector<SpriteSlices> undo, redo;
  int sel = -1;
  float zoom = 0;                    // 0 = fit on the next draw
  ImVec2 pan{0, 0};                  // screen offset of the texture's top-left from the canvas origin
  Drag drag = Drag::None;
  int handle = 0;
  int sx = 0, sy = 0;                // texel at the drag start
  SpriteRect startRect;
  SpriteRect fresh;                  // the rectangle being drawn (NewRect)
  bool gesturePushed = false;
  double lastPush = -100;
  char nameBuf[96] = {};
  int nameFor = -2;
  std::string error;
  // the canvas as last drawn, for the scripted mouse
  ImVec2 viewC0{0, 0};
  float viewZoom = 0;
  ImVec2 viewPan{0, 0};
  struct Sim {
    bool active = false;
    int phase = 0;
    std::vector<std::pair<float, float>> pts;   // texel coordinates (fractions allowed: 14.0 is the edge between texels 13 and 14)
  } sim;
};

SpriteEd& ed() {
  static SpriteEd* e = new SpriteEd();
  return *e;
}

AssetDatabase& db() { return assetUi().sync.db(); }

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::fflush(stdout);
}

// Verbs and buttons: one undo step per call.
template <class F>
void mutate(SpriteEd& s, F fn) {
  s.undo.push_back(s.cur);
  if (s.undo.size() > 100) s.undo.erase(s.undo.begin());
  s.redo.clear();
  fn();
  s.prev = s.cur;
  s.lastPush = -100;
}

// Typing in a field or dragging on the canvas changes `cur` directly; this turns the change into undo entries.
void trackUndo(SpriteEd& s) {
  if (s.cur == s.prev) return;
  const double now = ImGui::GetTime();
  const bool gesture = s.drag != Drag::None;
  const bool push = gesture ? !s.gesturePushed : now - s.lastPush > 0.6;
  if (push) {
    s.undo.push_back(s.prev);
    if (s.undo.size() > 100) s.undo.erase(s.undo.begin());
    s.redo.clear();
    s.lastPush = now;
    if (gesture) s.gesturePushed = true;
  }
  s.prev = s.cur;
}

bool stepUndo(SpriteEd& s, bool redo) {
  std::vector<SpriteSlices>& from = redo ? s.redo : s.undo;
  if (from.empty()) return false;
  (redo ? s.undo : s.redo).push_back(s.cur);
  s.cur = from.back();
  from.pop_back();
  s.prev = s.cur;
  if (s.sel >= static_cast<int>(s.cur.rects.size())) s.sel = static_cast<int>(s.cur.rects.size()) - 1;
  return true;
}

bool modified(const SpriteEd& s) { return !(s.cur == s.saved); }

const char* kPivotNames[9] = {"Top Left", "Top", "Top Right", "Left", "Center", "Right", "Bottom Left", "Bottom", "Bottom Right"};

bool loadInto(SpriteEd& s, const std::string& guid) {
  const AssetEntry* e = db().findGuid(guid);
  if (!e || e->kind != AssetKind::Texture) return false;
  int w = 0, h = 0;
  if (!pngFileSize(e->path, w, h)) { EditorLog::instance().warn("sprite editor: " + e->rel + " is not a readable PNG"); return false; }
  SpriteSlices sl;
  std::string err;
  if (!loadSlices(db(), guid, sl, err)) EditorLog::instance().warn("sprite editor: " + e->rel + ": " + err + " (starting empty)");
  if (s.guid != guid && !s.guid.empty() && modified(s)) EditorLog::instance().warn("sprite editor: unsaved slices of " + s.rel + " were dropped");
  s.guid = guid;
  s.rel = e->rel;
  s.stem = e->path.stem().string();
  s.texW = w;
  s.texH = h;
  s.cur = s.saved = s.prev = sl;
  s.undo.clear();
  s.redo.clear();
  s.sel = -1;
  s.zoom = 0;
  s.drag = Drag::None;
  s.nameFor = -2;
  s.error.clear();
  return true;
}

bool applyNow(SpriteEd& s) {
  std::string err;
  if (!s.cur.validate(s.texW, s.texH, err)) { s.error = err; return false; }
  if (!saveSlices(db(), s.guid, s.cur, err)) { s.error = err; return false; }
  s.saved = s.cur;
  s.error.clear();
  EditorLog::instance().info("sprite editor: " + std::to_string(s.cur.rects.size()) + " slice(s) saved to " + s.rel + ".meta");
  return true;
}

void revertNow(SpriteEd& s) {
  SpriteSlices sl;
  std::string err;
  if (!loadSlices(db(), s.guid, sl, err)) { s.error = err; return; }
  mutate(s, [&] { s.cur = sl; });
  if (s.sel >= static_cast<int>(s.cur.rects.size())) s.sel = -1;
  s.error.clear();
}

}  // namespace

void openSpriteEditor(App& a, const std::string& textureGuid) {
  (void)a;
  SpriteEd& s = ed();
  if (s.guid != textureGuid || s.texW == 0) {
    if (!loadInto(s, textureGuid)) return;
  }
  s.show = true;
  ImGui::SetWindowFocus("Sprite Editor");
}

// ---- inspector helpers -------------------------------------------------------------------------------------------------
bool applySliceByName(App& a, const std::vector<ComponentRef>& refs, const std::string& name) {
  if (refs.empty()) return false;
  const nlohmann::json tex = fieldValue(a.doc, refs[0], "texture");
  if (!tex.is_string()) return false;
  SpriteSlices sl;
  std::string err;
  if (!loadSlices(db(), tex.get<std::string>(), sl, err)) return false;
  const int idx = sl.find(name);
  if (idx < 0) return false;
  const SliceFields f = sliceFields(sl.rects[static_cast<size_t>(idx)]);
  a.doc.beginGesture();
  editFields(a.doc, refs, "srcX", f.srcX);
  editFields(a.doc, refs, "srcY", f.srcY);
  editFields(a.doc, refs, "srcW", f.srcW);
  editFields(a.doc, refs, "srcH", f.srcH);
  editFields(a.doc, refs, "pivotX", f.pivotX);
  editFields(a.doc, refs, "pivotY", f.pivotY);
  a.doc.endGesture();
  return true;
}

// ---- the window ----------------------------------------------------------------------------------------------------------
namespace {

ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

struct View {
  ImVec2 c0, size;
  float zoom;
  ImVec2 pan;
  ImVec2 toScreen(float tx, float ty) const { return ImVec2(c0.x + pan.x + tx * zoom, c0.y + pan.y + ty * zoom); }
  ImVec2 toTexel(ImVec2 p) const { return ImVec2((p.x - c0.x - pan.x) / zoom, (p.y - c0.y - pan.y) / zoom); }
};

void fitView(SpriteEd& s, const View& v, float& zoom, ImVec2& pan) {
  float z = std::min((v.size.x - 24) / static_cast<float>(s.texW), (v.size.y - 24) / static_cast<float>(s.texH));
  z = z >= 1 ? std::floor(z) : std::max(0.1f, z);
  zoom = std::clamp(z, 0.1f, 64.0f);
  pan = ImVec2((v.size.x - s.texW * zoom) * 0.5f, (v.size.y - s.texH * zoom) * 0.5f);
}

int hitHandle(const SpriteRect& r, const View& v, ImVec2 m) {
  const ImVec2 a = v.toScreen(static_cast<float>(r.x), static_cast<float>(r.y)), b = v.toScreen(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
  const float t = 6;
  const bool nearL = std::fabs(m.x - a.x) <= t, nearR = std::fabs(m.x - b.x) <= t, nearT = std::fabs(m.y - a.y) <= t, nearB = std::fabs(m.y - b.y) <= t;
  const bool inX = m.x >= a.x - t && m.x <= b.x + t, inY = m.y >= a.y - t && m.y <= b.y + t;
  int h = 0;
  if (nearL && inY) h |= kL;
  if (nearR && inY) h |= kR;
  if (nearT && inX) h |= kT;
  if (nearB && inX) h |= kB;
  return h;
}

void drawCanvas(App& a, SpriteEd& s) {
  ImGuiIO& io = ImGui::GetIO();
  View v;
  v.c0 = ImGui::GetCursorScreenPos();
  v.size = ImGui::GetContentRegionAvail();
  if (v.size.x < 50 || v.size.y < 50) return;
  if (s.zoom <= 0) fitView(s, v, s.zoom, s.pan);
  v.zoom = s.zoom;
  v.pan = s.pan;
  s.viewC0 = v.c0;
  s.viewZoom = s.zoom;
  s.viewPan = s.pan;
  ImGui::InvisibleButton("##canvas", v.size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(v.c0, v.c0 + v.size, true);
  dl->AddRectFilled(v.c0, v.c0 + v.size, ImGui::GetColorU32(brandToken("surface-2")));

  const ImVec2 t0 = v.toScreen(0, 0), t1 = v.toScreen(static_cast<float>(s.texW), static_cast<float>(s.texH));
  // checkerboard behind the image, so transparent pixels read as transparent
  {
    const float cell = 8;
    const ImU32 c1 = ImGui::GetColorU32(brandToken("surface")), c2 = ImGui::GetColorU32(brandToken("surface-2"));
    const float x0 = std::max(t0.x, v.c0.x), y0 = std::max(t0.y, v.c0.y), x1 = std::min(t1.x, v.c0.x + v.size.x), y1 = std::min(t1.y, v.c0.y + v.size.y);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), c1);
    for (float y = t0.y + std::floor((y0 - t0.y) / cell) * cell; y < y1; y += cell)
      for (float x = t0.x + std::floor((x0 - t0.x) / cell) * cell; x < x1; x += cell)
        if (((static_cast<int>(std::floor((x - t0.x) / cell)) + static_cast<int>(std::floor((y - t0.y) / cell))) & 1) != 0)
          dl->AddRectFilled(ImVec2(std::max(x, x0), std::max(y, y0)), ImVec2(std::min(x + cell, x1), std::min(y + cell, y1)), c2);
  }
  if (const Scene2DResources::Tex* tex = a.res2d.texture(s.guid)) {
    imageNearestBegin(dl);
    dl->AddImage(static_cast<ImTextureID>(tex->gl), t0, t1);
    imageNearestEnd(dl);
  }
  dl->AddRect(t0, t1, ImGui::GetColorU32(brandToken("border-strong")));

  // rectangles
  for (size_t i = 0; i < s.cur.rects.size(); ++i) {
    const SpriteRect& r = s.cur.rects[i];
    const ImVec2 p0 = v.toScreen(static_cast<float>(r.x), static_cast<float>(r.y)), p1 = v.toScreen(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
    const bool isSel = static_cast<int>(i) == s.sel;
    if (isSel) {
      ImVec4 f = brandToken("primary");
      f.w = 0.18f;
      dl->AddRectFilled(p0, p1, ImGui::GetColorU32(f));
    }
    ImVec4 lc = isSel ? brandToken("primary") : brandToken("info");
    lc.w = isSel ? 1.0f : 0.85f;
    dl->AddRect(p0, p1, ImGui::GetColorU32(lc), 0, 0, isSel ? 2.0f : 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(r.name.c_str());
    if (ts.x + 8 < p1.x - p0.x && ts.y + 4 < p1.y - p0.y) {
      dl->AddRectFilled(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p0.x + ts.x + 7, p0.y + ts.y + 3), ImGui::GetColorU32(brandToken("surface")));
      dl->AddText(ImVec2(p0.x + 4, p0.y + 2), ImGui::GetColorU32(brandToken("text")), r.name.c_str());
    }
  }
  if (s.drag == Drag::NewRect) {
    const ImVec2 p0 = v.toScreen(static_cast<float>(s.fresh.x), static_cast<float>(s.fresh.y));
    const ImVec2 p1 = v.toScreen(static_cast<float>(s.fresh.x + s.fresh.w), static_cast<float>(s.fresh.y + s.fresh.h));
    dl->AddRect(p0, p1, ImGui::GetColorU32(brandToken("primary")), 0, 0, 2.0f);
  }
  if (s.sel >= 0 && s.sel < static_cast<int>(s.cur.rects.size())) {
    const SpriteRect& r = s.cur.rects[static_cast<size_t>(s.sel)];
    const ImVec2 p0 = v.toScreen(static_cast<float>(r.x), static_cast<float>(r.y)), p1 = v.toScreen(static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h));
    const ImU32 hc = ImGui::GetColorU32(brandToken("primary"));
    const ImVec2 hs[8] = {p0, ImVec2((p0.x + p1.x) * 0.5f, p0.y), ImVec2(p1.x, p0.y), ImVec2(p0.x, (p0.y + p1.y) * 0.5f),
                          ImVec2(p1.x, (p0.y + p1.y) * 0.5f), ImVec2(p0.x, p1.y), ImVec2((p0.x + p1.x) * 0.5f, p1.y), p1};
    for (const ImVec2& h : hs) dl->AddRectFilled(ImVec2(h.x - 3, h.y - 3), ImVec2(h.x + 3, h.y + 3), hc);
    const ImVec2 pv = v.toScreen(static_cast<float>(r.x + r.pivotX), static_cast<float>(r.y + r.pivotY));
    dl->AddCircleFilled(pv, 5, ImGui::GetColorU32(brandToken("surface")));
    dl->AddCircle(pv, 5, ImGui::GetColorU32(brandToken("danger")), 0, 2.0f);
    dl->AddLine(ImVec2(pv.x - 8, pv.y), ImVec2(pv.x + 8, pv.y), ImGui::GetColorU32(brandToken("danger")));
    dl->AddLine(ImVec2(pv.x, pv.y - 8), ImVec2(pv.x, pv.y + 8), ImGui::GetColorU32(brandToken("danger")));
  }
  dl->PopClipRect();

  // zoom (wheel keeps the texel under the cursor fixed) and pan (middle / right drag, or Space + left drag)
  if (hovered && io.MouseWheel != 0) {
    const ImVec2 before = v.toTexel(io.MousePos);
    s.zoom = std::clamp(s.zoom * std::pow(1.2f, io.MouseWheel), 0.1f, 64.0f);
    s.pan = ImVec2(io.MousePos.x - v.c0.x - before.x * s.zoom, io.MousePos.y - v.c0.y - before.y * s.zoom);
    v.zoom = s.zoom;
    v.pan = s.pan;
  }
  const bool space = ImGui::IsKeyDown(ImGuiKey_Space);
  if (active && (io.MouseDown[1] || io.MouseDown[2] || (space && io.MouseDown[0]))) {
    s.pan.x += io.MouseDelta.x;
    s.pan.y += io.MouseDelta.y;
    return;
  }

  // mouse editing
  const ImVec2 tp = v.toTexel(io.MousePos);
  const int tx = std::clamp(static_cast<int>(std::floor(tp.x)), 0, s.texW), ty = std::clamp(static_cast<int>(std::floor(tp.y)), 0, s.texH);
  if (hovered && io.MouseClicked[0] && !space) {
    s.drag = Drag::None;
    s.gesturePushed = false;
    s.sx = tx;
    s.sy = ty;
    if (s.sel >= 0 && s.sel < static_cast<int>(s.cur.rects.size())) {
      const SpriteRect& r = s.cur.rects[static_cast<size_t>(s.sel)];
      const ImVec2 pv = v.toScreen(static_cast<float>(r.x + r.pivotX), static_cast<float>(r.y + r.pivotY));
      if (std::fabs(io.MousePos.x - pv.x) <= 7 && std::fabs(io.MousePos.y - pv.y) <= 7) {
        s.drag = Drag::Pivot;
      } else if (const int h = hitHandle(r, v, io.MousePos)) {
        s.drag = Drag::Resize;
        s.handle = h;
        s.startRect = r;
      }
    }
    if (s.drag == Drag::None) {
      const int idx = s.cur.indexAt(std::min(tx, s.texW - 1), std::min(ty, s.texH - 1));
      if (idx >= 0) {
        s.sel = idx;
        s.drag = Drag::Move;
        s.startRect = s.cur.rects[static_cast<size_t>(idx)];
      } else {
        s.sel = -1;
        s.drag = Drag::NewRect;
        s.fresh = SpriteRect();
        s.fresh.x = tx;
        s.fresh.y = ty;
      }
    }
  }
  if (s.drag != Drag::None) {
    if (!io.MouseDown[0]) {
      if (s.drag == Drag::NewRect && s.fresh.w >= 1 && s.fresh.h >= 1) {
        mutate(s, [&] {
          s.cur.mode = SliceMode::Manual;
          s.sel = s.cur.addRect(std::string(), s.fresh.x, s.fresh.y, s.fresh.w, s.fresh.h);
        });
      }
      s.drag = Drag::None;
    } else if (s.sel >= 0 && s.sel < static_cast<int>(s.cur.rects.size()) && s.drag != Drag::NewRect) {
      SpriteRect r = s.cur.rects[static_cast<size_t>(s.sel)];
      const int dx = tx - s.sx, dy = ty - s.sy;
      if (s.drag == Drag::Move) {
        r.x = std::clamp(s.startRect.x + dx, 0, std::max(0, s.texW - r.w));
        r.y = std::clamp(s.startRect.y + dy, 0, std::max(0, s.texH - r.h));
        if (r.x != s.cur.rects[static_cast<size_t>(s.sel)].x || r.y != s.cur.rects[static_cast<size_t>(s.sel)].y) {
          s.cur.mode = SliceMode::Manual;
          s.cur.setRect(s.sel, r.x, r.y, r.w, r.h);
        }
      } else if (s.drag == Drag::Resize) {
        int x0 = s.startRect.x, y0 = s.startRect.y, x1 = s.startRect.x + s.startRect.w, y1 = s.startRect.y + s.startRect.h;
        if (s.handle & kL) x0 = std::min(tx, x1 - 1);
        if (s.handle & kR) x1 = std::max(tx, x0 + 1);
        if (s.handle & kT) y0 = std::min(ty, y1 - 1);
        if (s.handle & kB) y1 = std::max(ty, y0 + 1);
        if (x0 != r.x || y0 != r.y || x1 - x0 != r.w || y1 - y0 != r.h) {
          s.cur.mode = SliceMode::Manual;
          s.cur.setRect(s.sel, x0, y0, x1 - x0, y1 - y0);
        }
      } else if (s.drag == Drag::Pivot) {
        const int px = static_cast<int>(std::lround(tp.x)) - r.x, py = static_cast<int>(std::lround(tp.y)) - r.y;
        s.cur.setPivot(s.sel, std::clamp(px, 0, r.w), std::clamp(py, 0, r.h));
      }
    } else if (s.drag == Drag::NewRect) {
      s.fresh.w = std::abs(tx - s.sx);
      s.fresh.h = std::abs(ty - s.sy);
      s.fresh.x = std::min(tx, s.sx);
      s.fresh.y = std::min(ty, s.sy);
    }
  }
  if (hovered && io.MouseWheel == 0 && !io.MouseDown[0]) ImGui::SetTooltip("texel %d, %d", std::min(tx, s.texW - 1), std::min(ty, s.texH - 1));
}

bool int2Field(const char* label, int* v, int lo, int hi) {
  ImGui::SetNextItemWidth(170);
  const bool c = ImGui::InputInt2(label, v);
  if (c) { v[0] = std::clamp(v[0], lo, hi); v[1] = std::clamp(v[1], lo, hi); }
  return c;
}

void drawActionBar(SpriteEd& s) {
  std::string verr;
  const bool valid = s.cur.validate(s.texW, s.texH, verr);
  const bool dirty = modified(s);
  ImGui::BeginDisabled(!dirty || !valid);
  ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary"));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
  if (ImGui::Button("Apply")) applyNow(s);
  ImGui::PopStyleColor(2);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Write the slices to the texture's .meta");
  ImGui::SameLine();
  ImGui::BeginDisabled(!dirty);
  if (ImGui::Button("Revert")) revertNow(s);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(s.undo.empty());
  if (ImGui::Button("Undo")) stepUndo(s, false);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(s.redo.empty());
  if (ImGui::Button("Redo")) stepUndo(s, true);
  ImGui::EndDisabled();
  if (!valid) ImGui::TextColored(brandToken("danger"), "[!] %s", verr.c_str());
  else if (!s.error.empty()) ImGui::TextColored(brandToken("danger"), "[!] %s", s.error.c_str());
  else if (dirty) ImGui::TextColored(brandToken("warn"), "[*] Unsaved changes");
  else ImGui::TextColored(brandToken("ok"), "Saved in the .meta");
}

void drawSidebar(App& a, SpriteEd& s) {
  (void)a;
  ImGui::TextUnformatted(s.rel.c_str());
  ImGui::TextDisabled("%d x %d px", s.texW, s.texH);
  drawActionBar(s);
  ImGui::Spacing();

  if (ImGui::CollapsingHeader("Grid slicing", ImGuiTreeNodeFlags_DefaultOpen)) {
    int cell[2] = {s.cur.cellW, s.cur.cellH}, pad[2] = {s.cur.paddingX, s.cur.paddingY}, off[2] = {s.cur.offsetX, s.cur.offsetY};
    if (int2Field("Cell size", cell, 0, s.texW)) { s.cur.cellW = cell[0]; s.cur.cellH = cell[1]; }
    if (int2Field("Padding", pad, 0, s.texW)) { s.cur.paddingX = pad[0]; s.cur.paddingY = pad[1]; }
    if (int2Field("Offset", off, 0, s.texW)) { s.cur.offsetX = off[0]; s.cur.offsetY = off[1]; }
    const int cols = SpriteSlices::gridColumns(s.texW, s.cur.cellW, s.cur.paddingX, s.cur.offsetX), rows = SpriteSlices::gridRows(s.texH, s.cur.cellH, s.cur.paddingY, s.cur.offsetY);
    ImGui::TextDisabled("%d columns x %d rows = %d cells", cols, rows, cols * rows);
    ImGui::BeginDisabled(cols * rows <= 0);
    if (ImGui::Button("Slice")) {
      mutate(s, [&] { s.cur.generateGrid(s.texW, s.texH, s.stem, true); });
      s.sel = -1;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Make a rectangle for every cell. Names and pivots of existing cells are kept.");
  }

  if (ImGui::CollapsingHeader("Sprites", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextDisabled("%s  -  %zu sprite(s)", s.cur.mode == SliceMode::Grid ? "grid" : "manual", s.cur.rects.size());
    ImGui::BeginChild("##list", ImVec2(0, 110), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < s.cur.rects.size(); ++i) {
      const SpriteRect& r = s.cur.rects[i];
      char l[160];
      std::snprintf(l, sizeof l, "%s   (%d,%d  %dx%d)###r%zu", r.name.c_str(), r.x, r.y, r.w, r.h, i);
      if (ImGui::Selectable(l, static_cast<int>(i) == s.sel)) s.sel = static_cast<int>(i);
    }
    ImGui::EndChild();
    if (ImGui::Button("Add")) {
      mutate(s, [&] {
        s.cur.mode = SliceMode::Manual;
        const int w = std::min(s.texW, s.cur.cellW > 0 ? s.cur.cellW : 16), h = std::min(s.texH, s.cur.cellH > 0 ? s.cur.cellH : 16);
        s.sel = s.cur.addRect(std::string(), 0, 0, w, h);
      });
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(s.sel < 0);
    if (ImGui::Button("Delete")) {
      mutate(s, [&] { s.cur.removeRect(s.sel); });
      s.sel = std::min(s.sel, static_cast<int>(s.cur.rects.size()) - 1);
    }
    ImGui::EndDisabled();
  }

  if (s.sel >= 0 && s.sel < static_cast<int>(s.cur.rects.size())) {
    ImGui::SeparatorText("Selected sprite");
    const int i = s.sel;
    SpriteRect r = s.cur.rects[static_cast<size_t>(i)];
    if (s.nameFor != i) { std::snprintf(s.nameBuf, sizeof s.nameBuf, "%s", r.name.c_str()); s.nameFor = i; }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##name", s.nameBuf, sizeof s.nameBuf, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
      if (!s.cur.renameRect(i, s.nameBuf)) {
        s.error = std::string("The name '") + s.nameBuf + "' is empty or already used";
        std::snprintf(s.nameBuf, sizeof s.nameBuf, "%s", r.name.c_str());
      } else s.error.clear();
    }
    int pos[2] = {r.x, r.y}, size[2] = {r.w, r.h}, pv[2] = {r.pivotX, r.pivotY};
    const bool p = int2Field("Position", pos, 0, s.texW), z = int2Field("Size", size, 1, s.texW);
    if (p || z) { s.cur.mode = SliceMode::Manual; s.cur.setRect(i, pos[0], pos[1], std::max(1, size[0]), std::max(1, size[1])); }
    if (int2Field("Pivot", pv, -4096, 4096)) s.cur.setPivot(i, pv[0], pv[1]);
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("Pivot preset", "choose...")) {
      for (int k = 0; k < 9; ++k)
        if (ImGui::Selectable(kPivotNames[k])) s.cur.setPivot(i, static_cast<PivotPreset>(k));
      ImGui::EndCombo();
    }
    ImGui::TextDisabled("Pivot: pixels from the sprite's top-left corner (drag the red dot).");
  }
}

}  // namespace

void drawSpriteEditor(App& a) {
  SpriteEd& s = ed();
  if (!a.loaded || !s.show) return;
  bindScene2dResources(a);
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(1040, 640), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 120, vp->WorkPos.y + 60), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Sprite Editor", &s.show)) { ImGui::End(); return; }
  if (s.guid.empty() || !db().findGuid(s.guid)) {
    ImGui::TextWrapped("No texture is open. Double-click a texture in the Project window, or pick one below.");
    for (const AssetEntry& e : db().entries())
      if (e.kind == AssetKind::Texture && ImGui::Selectable(e.rel.c_str())) loadInto(s, e.guid);
    ImGui::End();
    return;
  }
  ImGui::BeginChild("##side", ImVec2(330, 0), ImGuiChildFlags_Borders);
  drawSidebar(a, s);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginGroup();
  drawCanvas(a, s);
  ImGui::EndGroup();
  // Ctrl+Z / Ctrl+Y while this window is focused step the sprite editor's own history
  s.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if (s.focused && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) stepUndo(s, false);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) stepUndo(s, true);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && s.sel >= 0) {
      mutate(s, [&] { s.cur.removeRect(s.sel); });
      s.sel = std::min(s.sel, static_cast<int>(s.cur.rects.size()) - 1);
    }
  }
  trackUndo(s);
  ImGui::End();
}

// ---- scripted mouse (--ui-event spritedrag); positions are whole pixels (rounded up), so a texel edge lands on the texel it opens ----------------------------------------------------------------------------
bool spriteEditorFocused() { return ed().show && ed().focused; }
bool spriteSimHolds() { return ed().sim.active; }

void spriteSimFrame(App& a) {
  (void)a;
  SpriteEd& s = ed();
  if (!s.sim.active) return;
  ImGuiIO& io = ImGui::GetIO();
  if (!s.show || s.viewZoom <= 0 || s.sim.pts.empty()) { s.sim.active = false; return; }
  auto where = [&](size_t i) { return ImVec2(std::ceil(s.viewC0.x + s.viewPan.x + s.sim.pts[i].first * s.viewZoom), std::ceil(s.viewC0.y + s.viewPan.y + s.sim.pts[i].second * s.viewZoom)); };
  if (s.sim.phase == 0) {
    const ImVec2 p = where(0);
    io.AddMousePosEvent(p.x, p.y);
  } else if (s.sim.phase == 1) {
    io.AddMouseButtonEvent(0, true);
  } else if (static_cast<size_t>(s.sim.phase - 1) < s.sim.pts.size()) {
    const ImVec2 p = where(static_cast<size_t>(s.sim.phase - 1));
    io.AddMousePosEvent(p.x, p.y);
  } else {
    io.AddMouseButtonEvent(0, false);
    s.sim.active = false;
  }
  ++s.sim.phase;
}

// ---- --ui-event verbs --------------------------------------------------------------------------------------------------
bool spriteUiHook(App& a, const std::string& act, const std::string& arg) {
  SpriteEd& s = ed();
  EditorLog& log = EditorLog::instance();
  auto need = [&]() {
    if (s.guid.empty()) say("sprite: no texture is open (use spriteeditor:<texture path>)\n");
    return !s.guid.empty();
  };
  if (act == "spritedrag") {  // spritedrag:x0,y0,x1,y1[,x,y...]: a real mouse drag over the canvas, in texel coordinates
    if (!need()) return true;
    s.sim = SpriteEd::Sim();
    std::vector<float> v;
    size_t at = 0;
    while (at <= arg.size()) {
      const size_t e = arg.find(',', at);
      v.push_back(static_cast<float>(std::atof(arg.substr(at, e == std::string::npos ? std::string::npos : e - at).c_str())));
      if (e == std::string::npos) break;
      at = e + 1;
    }
    for (size_t i = 0; i + 1 < v.size(); i += 2) s.sim.pts.emplace_back(v[i], v[i + 1]);
    s.sim.active = s.sim.pts.size() >= 2;
    say("sprite: spritedrag %zu point(s)\n", s.sim.pts.size());
    return true;
  }
  if (act == "spriteclose") { s.show = false; return true; }
  if (act == "spritepick") { spritePickerOpenRequest() = true; return true; }
  if (act == "spritewindow") { s.show = true; return true; }   // Window > Sprite Editor with no texture picked
  if (act == "spriteeditor") {  // spriteeditor:<texture asset path>
    assetUiTick(a);
    const AssetEntry* e = db().findPath(arg);
    if (!e || e->kind != AssetKind::Texture) { log.warn("spriteeditor: not a texture: " + arg); say("sprite: editor texture not found: %s\n", arg.c_str()); return true; }
    if (!loadInto(s, e->guid)) { say("sprite: editor cannot open %s\n", arg.c_str()); return true; }
    s.show = true;
    say("sprite: editor %s %dx%d slices=%zu mode=%s\n", s.rel.c_str(), s.texW, s.texH, s.cur.rects.size(), s.cur.mode == SliceMode::Grid ? "grid" : "manual");
    return true;
  }
  if (act == "slicegrid") {  // slicegrid:w,h[,pad,offx,offy]
    if (!need()) return true;
    int v[5] = {0, 0, 0, 0, 0};
    const int n = std::sscanf(arg.c_str(), "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
    if (n < 2 || v[0] <= 0 || v[1] <= 0) { say("sprite: slicegrid:w,h[,pad,offx,offy]\n"); return true; }
    int count = 0;
    mutate(s, [&] {
      s.cur.cellW = v[0];
      s.cur.cellH = v[1];
      s.cur.paddingX = s.cur.paddingY = n > 2 ? v[2] : 0;
      s.cur.offsetX = n > 3 ? v[3] : 0;
      s.cur.offsetY = n > 4 ? v[4] : (n > 3 ? v[3] : 0);
      count = s.cur.generateGrid(s.texW, s.texH, s.stem, true);
    });
    s.sel = -1;
    say("sprite: slicegrid cell=%dx%d pad=%d,%d off=%d,%d cols=%d rows=%d -> %d slices\n", s.cur.cellW, s.cur.cellH, s.cur.paddingX, s.cur.paddingY, s.cur.offsetX,
        s.cur.offsetY, SpriteSlices::gridColumns(s.texW, s.cur.cellW, s.cur.paddingX, s.cur.offsetX),
        SpriteSlices::gridRows(s.texH, s.cur.cellH, s.cur.paddingY, s.cur.offsetY), count);
    return true;
  }
  if (act == "sliceapply") {
    if (!need()) return true;
    if (applyNow(s)) say("sprite: applied %zu slices to %s.meta\n", s.cur.rects.size(), s.rel.c_str());
    else say("sprite: apply failed: %s\n", s.error.c_str());
    return true;
  }
  if (act == "slicerevert") {
    if (!need()) return true;
    revertNow(s);
    say("sprite: reverted to %zu saved slices\n", s.cur.rects.size());
    return true;
  }
  if (act == "spriteundo" || act == "spriteredo") {
    if (!need()) return true;
    const bool ok = stepUndo(s, act == "spriteredo");
    say("sprite: %s %s slices=%zu\n", act.c_str(), ok ? "ok" : "nothing to do", s.cur.rects.size());
    return true;
  }
  if (act == "spriterect") {  // spriterect:x,y,w,h[,name]: a manual rectangle, like dragging one on the canvas
    if (!need()) return true;
    int x, y, w, h;
    char name[96] = {};
    const int n = std::sscanf(arg.c_str(), "%d,%d,%d,%d,%95s", &x, &y, &w, &h, name);
    if (n < 4) { say("sprite: spriterect:x,y,w,h[,name]\n"); return true; }
    int idx = -1;
    mutate(s, [&] {
      s.cur.mode = SliceMode::Manual;
      idx = s.cur.addRect(name, x, y, w, h);
    });
    s.sel = idx;
    say("sprite: spriterect index=%d %s\n", idx, idx >= 0 ? s.cur.rects[static_cast<size_t>(idx)].name.c_str() : "refused");
    return true;
  }
  if (act == "spritename") {  // spritename:<index>|<new name>
    if (!need()) return true;
    const size_t bar = arg.find('|');
    bool ok = false;
    if (bar != std::string::npos) mutate(s, [&] { ok = s.cur.renameRect(std::atoi(arg.c_str()), arg.substr(bar + 1)); });
    s.nameFor = -2;
    say("sprite: spritename %s\n", ok ? "ok" : "refused");
    return true;
  }
  if (act == "spritepivot") {  // spritepivot:<index>|<Top Left|Top|...|Bottom Right|x,y>
    if (!need()) return true;
    const size_t bar = arg.find('|');
    bool ok = false;
    if (bar != std::string::npos) {
      const int i = std::atoi(arg.c_str());
      const std::string v = arg.substr(bar + 1);
      int px, py;
      mutate(s, [&] {
        if (std::sscanf(v.c_str(), "%d,%d", &px, &py) == 2) ok = s.cur.setPivot(i, px, py);
        else for (int k = 0; k < 9; ++k) if (v == kPivotNames[k]) ok = s.cur.setPivot(i, static_cast<PivotPreset>(k));
      });
      if (ok) say("sprite: spritepivot %d -> %d,%d\n", i, s.cur.rects[static_cast<size_t>(i)].pivotX, s.cur.rects[static_cast<size_t>(i)].pivotY);
    }
    if (!ok) say("sprite: spritepivot refused\n");
    return true;
  }
  if (act == "spriteselect") {
    if (!need()) return true;
    int i = s.cur.find(arg);
    if (i < 0) i = std::atoi(arg.c_str());
    s.sel = i >= 0 && i < static_cast<int>(s.cur.rects.size()) ? i : -1;
    s.nameFor = -2;
    return true;
  }
  if (act == "spritedump") {
    if (!need()) return true;
    say("sprite: dump %s mode=%s slices=%zu modified=%d undo=%zu\n", s.rel.c_str(), s.cur.mode == SliceMode::Grid ? "grid" : "manual", s.cur.rects.size(), modified(s) ? 1 : 0, s.undo.size());
    for (size_t i = 0; i < s.cur.rects.size(); ++i) {
      const SpriteRect& r = s.cur.rects[i];
      say("sprite: slice %zu %s %d,%d %dx%d pivot %d,%d\n", i, r.name.c_str(), r.x, r.y, r.w, r.h, r.pivotX, r.pivotY);
    }
    return true;
  }
  if (act == "setsprite") {  // setsprite:<object>|<slice name>[|<texture asset path>]: the SpriteRenderer picker (saved slices of the .meta)
    const size_t b1 = arg.find('|');
    if (b1 == std::string::npos) { say("sprite: setsprite:<object>|<slice name>[|<texture path>]\n"); return true; }
    const std::string objName = arg.substr(0, b1);
    std::string rest = arg.substr(b1 + 1), texPath;
    const size_t b2 = rest.find('|');
    if (b2 != std::string::npos) { texPath = rest.substr(b2 + 1); rest = rest.substr(0, b2); }
    std::string guid;
    for (const SceneObject& o : a.doc.scene().objects) if (o.name == objName) guid = o.guid;
    std::vector<ComponentRef> refs = sameTypeAcross(a.doc, {guid}, "SpriteRenderer");
    if (guid.empty() || refs.empty()) { say("sprite: setsprite: %s has no SpriteRenderer\n", objName.c_str()); return true; }
    if (!texPath.empty()) {
      assetUiTick(a);
      const AssetEntry* t = db().findPath(texPath);
      if (!t) { say("sprite: setsprite: texture %s not found\n", texPath.c_str()); return true; }
      editFields(a.doc, refs, "texture", t->guid);
    }
    const bool ok = applySliceByName(a, refs, rest);
    if (ok) {
      const nlohmann::json x = fieldValue(a.doc, refs[0], "srcX"), y = fieldValue(a.doc, refs[0], "srcY"), w = fieldValue(a.doc, refs[0], "srcW"),
                           h = fieldValue(a.doc, refs[0], "srcH"), px = fieldValue(a.doc, refs[0], "pivotX"), py = fieldValue(a.doc, refs[0], "pivotY");
      say("sprite: setsprite %s slice=%s src=%d,%d,%d,%d pivot=%d,%d\n", objName.c_str(), rest.c_str(), x.get<int>(), y.get<int>(), w.get<int>(), h.get<int>(),
          px.get<int>(), py.get<int>());
    } else {
      say("sprite: setsprite: no slice '%s' in the texture's .meta (Apply the slices first)\n", rest.c_str());
    }
    return true;
  }
  return false;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(sprite_editor, "Sprite Editor", drawSpriteEditor, spriteUiHook, [](App&, const std::string&) { return spriteSimHolds(); }, 150, 120, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
