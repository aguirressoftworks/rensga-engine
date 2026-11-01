// F6.04 Editor 10: UI in the Scene view. In 2D mode the scene's canvas is drawn over the Scene window at the target's resolution
// (what the runtime draws: Canvas scaling, layout groups, slider fills, image fill), UI elements are picked, moved and resized with
// rect handles (edits go through SceneDocument, one drag = one undo step), and GameObject > UI creates the elements.
//
// --ui-event verbs (output lines start with "ui:"):
//   uicreate:<Canvas|Text|Image|Button|Slider|Toggle|Layout|EventSystem>[|<parent name>[|<name>]]   create like the menu (a Button named
//                                    PlayButton gets the child PlayButtonLabel, a Slider VolumeFill and VolumeHandle, a Toggle ...Check and ...Label)
//   uiprops:<object>|<Type>.<field>=<json>[;<Type>.<field>=<json>...]   set fields of an object by name (one undo step)
//   uiref:<object>|<Type>.<field>|<target object>      set a Guid field to the target's GUID (EventSystem.firstSelected, UIButton.selectOnDown, ...)
//   uievent:<object>|<Type>.<field>|<target object>|<TargetType>.<Method>[|dynamic|<json arg>]   add a UnityEvent listener
//                                    (dynamic = the method gets the event's value, e.g. Slider.onValueChanged -> SetVolume)
//   uiactive:<object>|0|1             SetActive
//   uishow:on|off                    the canvas overlay
//   uidump                           view size, scale, every element with its real and virtual rect and what drives it
//   uimove:<name>|dx,dy              move by virtual pixels, the same code as a mouse drag
//   uiresize:<name>|<tl|t|tr|l|r|bl|b|br>|dx,dy   drag a rect handle by virtual pixels
//   uiclick:<name>                   click the centre of the element through the overlay hit test (selects what is on top there)
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "panel_registry.h"
#include "panels/canvas_edit.h"
#include "rensga/core/reflect_method.h"
#include "rensga/editor/target_module.h"
#include "rensga/editor/ui_edit.h"

namespace rensga {
namespace editor {

namespace {

struct CanvasEditState {
  UiPreview preview;
  bool show = true;
  bool built = false;
  int viewW = 480, viewH = 272;
  std::vector<std::pair<ImVec2, ImVec2>> hits;  // screen rects of the last frame (elements and handles)
  // drag
  int mode = 0;               // 0 none, 1 move, 2 resize
  std::string guid;
  int handle = 0;             // 0..7: tl t tr l r bl b br
  ImVec2 start{0, 0};
  int startX = 0, startY = 0, startW = 0, startH = 0, anchor = 0, component = -1;
  float vscale = 1;
  int lastDx = 0, lastDy = 0;
  bool gesture = false;
};
CanvasEditState g_ce;

const char* kHandleNames[8] = {"tl", "t", "tr", "l", "r", "bl", "b", "br"};

void targetView(App& a, int& w, int& h) {
  const ConsoleTargetData& ct = consoleTargetFor(a.project.root);
  w = ct.width > 0 ? static_cast<int>(ct.width) : 480;
  h = ct.height > 0 ? static_cast<int>(ct.height) : 272;
}

int fieldInt(const SceneObject* o, int comp, const char* f, int def) {
  if (!o || comp < 0 || comp >= static_cast<int>(o->components.size())) return def;
  const auto& fs = o->components[static_cast<size_t>(comp)].fields;
  auto it = fs.find(f);
  return it != fs.end() && it->is_number() ? it->get<int>() : def;
}

void beginDrag(App& a, const UiElement& e, int mode, int handle, ImVec2 mouse) {
  const SceneObject* o = a.doc.find(e.guid);
  g_ce.mode = mode;
  g_ce.guid = e.guid;
  g_ce.handle = handle;
  g_ce.start = mouse;
  g_ce.component = e.component;
  g_ce.anchor = e.anchor;
  g_ce.startX = fieldInt(o, e.component, "x", 0);
  g_ce.startY = fieldInt(o, e.component, "y", 0);
  g_ce.startW = fieldInt(o, e.component, "w", static_cast<int>(e.vw));
  g_ce.startH = fieldInt(o, e.component, "h", static_cast<int>(e.vh));
  g_ce.vscale = std::max(0.01f, g_ce.preview.view().scale);
  g_ce.lastDx = g_ce.lastDy = 0;
  a.doc.beginGesture();
  g_ce.gesture = true;
}

// The drag in virtual pixels; used by the mouse and by the uimove / uiresize verbs.
void applyDrag(App& a, int dx, int dy) {
  if (g_ce.mode == 0 || g_ce.component < 0) return;
  if (g_ce.mode == 1) {
    a.doc.setField(g_ce.guid, g_ce.component, "x", g_ce.startX + dx);
    a.doc.setField(g_ce.guid, g_ce.component, "y", g_ce.startY + dy);
    return;
  }
  // Resize: the edges named by the handle move, the others stay; the result goes back through the anchor math.
  const int vw = g_ce.preview.view().vw, vh = g_ce.preview.view().vh;
  int r[4];
  uiResolve(g_ce.anchor, g_ce.startX, g_ce.startY, g_ce.startW, g_ce.startH, vw, vh, r);
  int left = r[0], top = r[1], right = r[0] + r[2], bottom = r[1] + r[3];
  const int h = g_ce.handle;
  const bool L = h == 0 || h == 3 || h == 5, R = h == 2 || h == 4 || h == 7, T = h == 0 || h == 1 || h == 2, B = h == 5 || h == 6 || h == 7;
  if (L) left += dx;
  if (R) right += dx;
  if (T) top += dy;
  if (B) bottom += dy;
  if (right - left < 2) { if (L) left = right - 2; else right = left + 2; }
  if (bottom - top < 2) { if (T) top = bottom - 2; else bottom = top + 2; }
  int x, y;
  uiPlaceTopLeft(g_ce.anchor, left, top, right - left, bottom - top, vw, vh, x, y);
  a.doc.setField(g_ce.guid, g_ce.component, "x", x);
  a.doc.setField(g_ce.guid, g_ce.component, "y", y);
  a.doc.setField(g_ce.guid, g_ce.component, "w", right - left);
  a.doc.setField(g_ce.guid, g_ce.component, "h", bottom - top);
}

void endDrag(App& a) {
  if (g_ce.gesture) a.doc.endGesture();
  g_ce.gesture = false;
  g_ce.mode = 0;
}

void ensurePreview(App& a) {
  targetView(a, g_ce.viewW, g_ce.viewH);
  g_ce.built = a.doc.loaded() && g_ce.preview.rebuild(a.doc, g_ce.viewW, g_ce.viewH);
}

std::vector<std::string> splitBar(const std::string& arg, char sep = '|') {
  std::vector<std::string> p;
  size_t at = 0;
  while (true) { size_t e = arg.find(sep, at); p.push_back(arg.substr(at, e == std::string::npos ? e : e - at)); if (e == std::string::npos) break; at = e + 1; }
  return p;
}

int compIndexByType(const SceneObject* o, const std::string& type) {
  if (!o) return -1;
  for (size_t i = 0; i < o->components.size(); ++i) if (o->components[i].type == type) return static_cast<int>(i);
  return -1;
}

std::string guidOfName(App& a, const std::string& name) {
  for (const SceneObject& o : a.doc.scene().objects) if (o.name == name) return o.guid;
  return std::string();
}

ImU32 col(const ImVec4& c, float alpha = -1) {
  ImVec4 v = c;
  if (alpha >= 0) v.w = alpha;
  return ImGui::GetColorU32(v);
}

void drawCanvasEdit(App& a) {
  g_ce.hits.clear();
  if (!a.loaded || !a.doc.loaded() || !a.showScene || !g_ce.show || !a.sv.mode2d) { if (g_ce.mode) endDrag(a); return; }
  if (a.session.active()) return;
  ensurePreview(a);
  if (!g_ce.built) { if (g_ce.mode) endDrag(a); return; }
  const ImVec2 p0 = a.sv.lastP0, sz = a.sv.lastSize;
  if (sz.x < 8 || sz.y < 8) return;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const bool open = ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!open) { ImGui::End(); return; }
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
  dl->PushClipRect(p0, p1, true);
  const float ppu = a.sv.cam2.ppu;
  const float cx = a.sv.cam2.center[0], cy = a.sv.cam2.center[1];
  auto sx = [&](float wx) { return p0.x + (wx - cx) * ppu + sz.x * 0.5f; };
  auto sy = [&](float wy) { return p0.y + (wy - cy) * ppu + sz.y * 0.5f; };
  const UiPreviewView& pv = g_ce.preview.view();

  // canvas frame at the target's resolution
  dl->AddRect(ImVec2(sx(0), sy(0)), ImVec2(sx(static_cast<float>(g_ce.viewW)), sy(static_cast<float>(g_ce.viewH))), col(brandToken("text-2"), 0.9f), 0, 0, 2.0f);
  char label[96];
  std::snprintf(label, sizeof label, "Canvas %dx%d%s", g_ce.viewW, g_ce.viewH,
                pv.hasCanvas && pv.scaleMode == 1 ? "  (reference scaled)" : (pv.hasCanvas ? "" : "  (no Canvas object)"));
  dl->AddText(ImVec2(sx(0), sy(0) - ImGui::GetTextLineHeight() - 3), col(brandToken("text-2")), label);

  // draw order: sorting order, then scene order
  std::vector<const UiElement*> order;
  for (const UiElement& e : g_ce.preview.elements()) order.push_back(&e);
  std::stable_sort(order.begin(), order.end(), [](const UiElement* x, const UiElement* y) { return x->sorting < y->sorting; });
  const std::vector<std::string> sel = a.doc.selection();
  auto isSel = [&](const std::string& g) { return std::find(sel.begin(), sel.end(), g) != sel.end(); };
  const float fontPx = std::max(7.0f, 8.0f * ppu);
  for (const UiElement* e : order) {
    ImVec2 mn(sx(e->x), sy(e->y)), mx(sx(e->x + e->w), sy(e->y + e->h));
    const ImU32 c = IM_COL32(e->color[0], e->color[1], e->color[2], e->color[3]);
    if (e->kind == UiElement::Text) {
      dl->AddText(nullptr, fontPx, mn, c, e->text.c_str());
      mx = ImVec2(mn.x + static_cast<float>(e->text.size()) * 8.0f * ppu, mn.y + 8.0f * ppu);
    } else if (e->kind == UiElement::Button) {
      dl->AddRectFilled(mn, mx, c);
      dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 40));
    } else {
      ImVec2 fmn = mn, fmx = mx;
      if (e->fillMethod == 1) { const float f = std::clamp(e->fillAmount, 0.0f, 1.0f); if (e->fillFromEnd) fmn.x = mx.x - (mx.x - mn.x) * f; else fmx.x = mn.x + (mx.x - mn.x) * f; }
      else if (e->fillMethod == 2) { const float f = std::clamp(e->fillAmount, 0.0f, 1.0f); if (e->fillFromEnd) fmx.y = mn.y + (mx.y - mn.y) * f; else fmn.y = mx.y - (mx.y - mn.y) * f; }
      if (e->fillMethod != 0) dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 50));   // the whole image as an outline, the filled part solid
      dl->AddRectFilled(fmn, fmx, c);
      if (e->textured) { dl->AddLine(fmn, fmx, IM_COL32(0, 0, 0, 90)); dl->AddLine(ImVec2(fmn.x, fmx.y), ImVec2(fmx.x, fmn.y), IM_COL32(0, 0, 0, 90)); }
    }
    g_ce.hits.push_back({mn, mx});
    if (isSel(e->guid)) {
      const bool primary = e->guid == a.doc.selected();
      dl->AddRect(ImVec2(mn.x - 1, mn.y - 1), ImVec2(mx.x + 1, mx.y + 1), col(brandToken("primary")), 0, 0, primary ? 2.0f : 1.0f);
      if (e->driven) dl->AddText(ImVec2(mn.x, mx.y + 2), col(brandToken("warn")), ("driven by " + e->drivenBy).c_str());
    }
  }

  // handles of the primary selection (not for driven elements: the runtime overwrites them)
  const UiElement* prim = g_ce.preview.find(a.doc.selected());
  struct HandleBox { ImVec2 c; };
  HandleBox hb[8];
  bool haveHandles = false;
  if (prim && prim->hasSize && !prim->driven && prim->kind != UiElement::Text) {
    const ImVec2 mn(sx(prim->x), sy(prim->y)), mx(sx(prim->x + prim->w), sy(prim->y + prim->h));
    const float midx = (mn.x + mx.x) * 0.5f, midy = (mn.y + mx.y) * 0.5f;
    const ImVec2 pts[8] = {{mn.x, mn.y}, {midx, mn.y}, {mx.x, mn.y}, {mn.x, midy}, {mx.x, midy}, {mn.x, mx.y}, {midx, mx.y}, {mx.x, mx.y}};
    for (int i = 0; i < 8; ++i) {
      hb[i].c = pts[i];
      dl->AddRectFilled(ImVec2(pts[i].x - 4, pts[i].y - 4), ImVec2(pts[i].x + 4, pts[i].y + 4), col(brandToken("primary")));
      dl->AddRect(ImVec2(pts[i].x - 4, pts[i].y - 4), ImVec2(pts[i].x + 4, pts[i].y + 4), IM_COL32(0, 0, 0, 200));
      g_ce.hits.push_back({ImVec2(pts[i].x - 6, pts[i].y - 6), ImVec2(pts[i].x + 6, pts[i].y + 6)});
    }
    haveHandles = true;
  }
  dl->PopClipRect();

  // ---- mouse ----
  const bool hot = ImGui::IsWindowHovered() && io.MousePos.x >= p0.x && io.MousePos.x < p1.x && io.MousePos.y >= p0.y && io.MousePos.y < p1.y;
  if (g_ce.mode != 0) {
    if (io.MouseDown[0]) {
      const float k = 1.0f / (ppu * g_ce.vscale);
      const int dx = static_cast<int>(std::lround((io.MousePos.x - g_ce.start.x) * k)), dy = static_cast<int>(std::lround((io.MousePos.y - g_ce.start.y) * k));
      if (dx != g_ce.lastDx || dy != g_ce.lastDy) { applyDrag(a, dx, dy); g_ce.lastDx = dx; g_ce.lastDy = dy; }
    } else {
      endDrag(a);
    }
  } else if (hot && io.MouseClicked[0] && !io.KeyAlt && a.sv.drag == 0 && !ImGuizmo::IsUsing()) {
    bool took = false;
    if (haveHandles && prim) {
      for (int i = 0; i < 8 && !took; ++i) {
        if (std::fabs(io.MousePos.x - hb[i].c.x) <= 6 && std::fabs(io.MousePos.y - hb[i].c.y) <= 6) { beginDrag(a, *prim, 2, i, io.MousePos); took = true; }
      }
    }
    if (!took) {
      for (auto it = order.rbegin(); it != order.rend() && !took; ++it) {
        const UiElement* e = *it;
        float w = e->w, h = e->h;
        if (e->kind == UiElement::Text) { w = static_cast<float>(e->text.size()) * 8.0f; h = 8.0f; }
        const ImVec2 mn(sx(e->x), sy(e->y)), mx(sx(e->x + w), sy(e->y + h));
        if (io.MousePos.x >= mn.x && io.MousePos.x < mx.x && io.MousePos.y >= mn.y && io.MousePos.y < mx.y) {
          if (io.KeyCtrl) a.doc.toggleSelect(e->guid);
          else if (io.KeyShift) a.doc.addToSelection(e->guid);
          else a.doc.select(e->guid);
          if (!e->driven && e->component >= 0 && !io.KeyCtrl && !io.KeyShift) beginDrag(a, *e, 1, 0, io.MousePos);
          took = true;
        }
      }
    }
  }
  ImGui::End();
}

bool canvasHook(App& a, const std::string& act, const std::string& arg) {
  if (act == "uishow") { g_ce.show = arg != "off"; std::printf("ui: overlay %s\n", g_ce.show ? "on" : "off"); std::fflush(stdout); return true; }
  if (act == "uicreate") {
    const size_t bar = arg.find('|');
    UiKind k;
    if (!uiKindFromName(arg.substr(0, bar), k)) { std::printf("ui: unknown kind '%s'\n", arg.substr(0, bar).c_str()); std::fflush(stdout); return true; }
    const std::vector<std::string> cp = splitBar(arg);
    std::string parent = cp.size() > 1 && !cp[1].empty() ? guidOfName(a, cp[1]) : std::string();
    int w, h;
    targetView(a, w, h);
    const std::string g = createUiObject(a.doc, k, parent, w, h, cp.size() > 2 ? cp[2] : std::string());
    const SceneObject* o = a.doc.find(g);
    std::printf("ui: created %s '%s' objects=%zu\n", uiKindName(k), o ? o->name.c_str() : "", a.doc.scene().objects.size());
    std::fflush(stdout);
    return true;
  }
  if (act == "uidump") {
    ensurePreview(a);
    const UiPreviewView& v = g_ce.preview.view();
    std::printf("ui: view %dx%d scale=%.3f virtual=%dx%d canvas=%d reference=%dx%d mode=%d elements=%zu\n", v.viewW, v.viewH, static_cast<double>(v.scale), v.vw, v.vh,
                v.hasCanvas ? 1 : 0, v.referenceW, v.referenceH, v.scaleMode, g_ce.preview.elements().size());
    for (const UiElement& e : g_ce.preview.elements())
      std::printf("ui:   %-14s %-6s rect=%.0f,%.0f,%.0f,%.0f virtual=%.0f,%.0f,%.0f,%.0f%s%s%s\n", e.name.c_str(),
                  e.kind == UiElement::Button ? "button" : (e.kind == UiElement::Text ? "text" : "image"), static_cast<double>(e.x), static_cast<double>(e.y),
                  static_cast<double>(e.w), static_cast<double>(e.h), static_cast<double>(e.vx), static_cast<double>(e.vy), static_cast<double>(e.vw),
                  static_cast<double>(e.vh), e.text.empty() ? "" : (" text=\"" + e.text + "\"").c_str(), e.driven ? (" driven=" + e.drivenBy).c_str() : "",
                  e.fillMethod ? " fill" : "");
    std::fflush(stdout);
    return true;
  }
  if (act == "uiprops") {
    const std::vector<std::string> p = splitBar(arg);
    const std::string g = guidOfName(a, p[0]);
    const SceneObject* o = a.doc.find(g);
    int n = 0, bad = 0;
    if (o && p.size() > 1) {
      a.doc.beginGesture();
      for (const std::string& item : splitBar(p[1], ';')) {
        const size_t dot = item.find('.'), eq = item.find('=');
        if (dot == std::string::npos || eq == std::string::npos || eq < dot) { ++bad; continue; }
        const int ci = compIndexByType(a.doc.find(g), item.substr(0, dot));
        nlohmann::json v = nlohmann::json::parse(item.substr(eq + 1), nullptr, false);
        if (ci < 0 || v.is_discarded() || !a.doc.setField(g, ci, item.substr(dot + 1, eq - dot - 1), v)) ++bad; else ++n;
      }
      a.doc.endGesture();
    } else bad = 1;
    std::printf("ui: props %s set=%d refused=%d\n", p[0].c_str(), n, bad);
    std::fflush(stdout);
    return true;
  }
  if (act == "uiref") {
    const std::vector<std::string> p = splitBar(arg);
    const std::string g = guidOfName(a, p[0]), t = p.size() > 2 ? guidOfName(a, p[2]) : std::string();
    const size_t dot = p.size() > 1 ? p[1].find('.') : std::string::npos;
    bool ok = !g.empty() && !t.empty() && dot != std::string::npos;
    if (ok) { const int ci = compIndexByType(a.doc.find(g), p[1].substr(0, dot)); ok = ci >= 0 && a.doc.setField(g, ci, p[1].substr(dot + 1), t); }
    std::printf("ui: ref %s %s %s\n", p[0].c_str(), p.size() > 1 ? p[1].c_str() : "", ok ? "ok" : "refused");
    std::fflush(stdout);
    return true;
  }
  if (act == "uiactive") {
    const std::vector<std::string> p = splitBar(arg);
    const std::string g = guidOfName(a, p[0]);
    const bool ok = !g.empty() && p.size() > 1 && a.doc.setActive(g, p[1] != "0");
    std::printf("ui: active %s %s\n", p[0].c_str(), ok ? "ok" : "refused");
    std::fflush(stdout);
    return true;
  }
  if (act == "uievent") {
    const std::vector<std::string> p = splitBar(arg);
    std::string why;
    const std::string g = p.size() > 3 ? guidOfName(a, p[0]) : std::string(), tg = p.size() > 3 ? guidOfName(a, p[2]) : std::string();
    const size_t d1 = p.size() > 1 ? p[1].find('.') : std::string::npos, d2 = p.size() > 3 ? p[3].find('.') : std::string::npos;
    if (p.size() < 4 || g.empty() || tg.empty() || d1 == std::string::npos || d2 == std::string::npos) why = "needs <object>|<Type>.<field>|<target>|<Type>.<Method>";
    else {
      const int ci = compIndexByType(a.doc.find(g), p[1].substr(0, d1));
      const std::string field = p[1].substr(d1 + 1), tType = p[3].substr(0, d2), method = p[3].substr(d2 + 1);
      const TypeInfo* ti = TypeRegistry::instance().find(tType.c_str());
      const MethodInfo* mi = ti ? findMethod(*ti, method.c_str()) : nullptr;
      if (ci < 0) why = "no such component";
      else if (!ti) why = "unknown component type " + tType + " (is the script built?)";
      else if (!mi) why = "no METHOD(" + method + ") on " + tType;
      else if (compIndexByType(a.doc.find(tg), tType) < 0) why = "the target has no " + tType;
      else {
        nlohmann::json arr = a.doc.find(g)->components[static_cast<size_t>(ci)].fields.value(field, nlohmann::json::array());
        if (!arr.is_array()) arr = nlohmann::json::array();
        nlohmann::json l = {{"target", tg}, {"type", tType}, {"method", method}};
        if (p.size() > 4 && p[4] == "dynamic") l["dynamic"] = true;
        else if (p.size() > 4 && !p[4].empty() && mi->arg != MethodArgKind::None) { nlohmann::json v = nlohmann::json::parse(p[4], nullptr, false); if (!v.is_discarded()) l["arg"] = v; }
        arr.push_back(l);
        if (!a.doc.setField(g, ci, field, arr)) why = "the document refused the event";
      }
    }
    std::printf("ui: event %s %s\n", p[0].c_str(), why.empty() ? "ok" : ("refused: " + why).c_str());
    std::fflush(stdout);
    return true;
  }
  if (act == "uimove" || act == "uiresize") {
    std::vector<std::string> p;
    size_t at = 0;
    while (true) { size_t e = arg.find('|', at); p.push_back(arg.substr(at, e == std::string::npos ? e : e - at)); if (e == std::string::npos) break; at = e + 1; }
    const bool move = act == "uimove";
    int dx = 0, dy = 0;
    const std::string deltas = move ? (p.size() > 1 ? p[1] : "") : (p.size() > 2 ? p[2] : "");
    std::sscanf(deltas.c_str(), "%d,%d", &dx, &dy);
    ensurePreview(a);
    const UiElement* e = g_ce.preview.find(guidOfName(a, p[0]));
    int handle = 7;
    if (!move && p.size() > 1) for (int i = 0; i < 8; ++i) if (p[1] == kHandleNames[i]) handle = i;
    if (!e || e->driven || e->component < 0) { std::printf("ui: %s '%s' refused (%s)\n", act.c_str(), p[0].c_str(), !e ? "no such element" : "driven by the runtime"); std::fflush(stdout); return true; }
    beginDrag(a, *e, move ? 1 : 2, handle, ImVec2(0, 0));
    applyDrag(a, dx, dy);
    endDrag(a);
    ensurePreview(a);
    const SceneObject* o = a.doc.find(e->guid);
    std::printf("ui: %s %s x=%d y=%d w=%d h=%d\n", move ? "moved" : "resized", p[0].c_str(), fieldInt(o, e->component, "x", 0), fieldInt(o, e->component, "y", 0),
                fieldInt(o, e->component, "w", -1), fieldInt(o, e->component, "h", -1));
    std::fflush(stdout);
    return true;
  }
  if (act == "uiclick") {
    ensurePreview(a);
    const UiElement* t = g_ce.preview.find(guidOfName(a, arg));
    if (!t) { std::printf("ui: click '%s': no such element\n", arg.c_str()); std::fflush(stdout); return true; }
    const float px = t->x + t->w * 0.5f, py = t->y + t->h * 0.5f;
    const UiElement* hit = nullptr;
    std::vector<const UiElement*> order;
    for (const UiElement& e : g_ce.preview.elements()) order.push_back(&e);
    std::stable_sort(order.begin(), order.end(), [](const UiElement* x, const UiElement* y) { return x->sorting < y->sorting; });
    for (auto it = order.rbegin(); it != order.rend() && !hit; ++it) {
      const UiElement* e = *it;
      const float w = e->kind == UiElement::Text ? static_cast<float>(e->text.size()) * 8.0f : e->w, h = e->kind == UiElement::Text ? 8.0f : e->h;
      if (px >= e->x && px < e->x + w && py >= e->y && py < e->y + h) hit = e;
    }
    if (hit) a.doc.select(hit->guid);
    std::printf("ui: click %s -> %s\n", arg.c_str(), hit ? hit->name.c_str() : "(nothing)");
    std::fflush(stdout);
    return true;
  }
  return false;
}

}  // namespace

bool canvasEditClaimsMouse() {
  if (g_ce.mode != 0) return true;
  const ImVec2 m = ImGui::GetIO().MousePos;
  for (const auto& r : g_ce.hits) if (m.x >= r.first.x && m.x < r.second.x && m.y >= r.first.y && m.y < r.second.y) return true;
  return false;
}

void drawUiCreateMenuItems(App& a, const std::string& parent, const float*) {
  const bool ok = a.doc.loaded();
  static const struct { const char* label; UiKind kind; } kItems[] = {{"Canvas", UiKind::Canvas}, {"Text", UiKind::Text}, {"Image", UiKind::Image},
                                                                      {"Button", UiKind::Button}, {"Slider", UiKind::Slider}, {"Toggle", UiKind::Toggle},
                                                                      {"Layout Group", UiKind::LayoutGroup}, {"EventSystem", UiKind::EventSystem}};
  for (const auto& it : kItems) {
    if (it.kind == UiKind::LayoutGroup || it.kind == UiKind::EventSystem) { /* keep the order of Unity's UI menu */ }
    if (ImGui::MenuItem(it.label, nullptr, false, ok)) {
      int w, h;
      targetView(a, w, h);
      createUiObject(a.doc, it.kind, parent, w, h);
    }
  }
}

RENSGA_EDITOR_PANEL(canvas_edit, "Canvas editing", drawCanvasEdit, canvasHook, nullptr, 61, 95, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
