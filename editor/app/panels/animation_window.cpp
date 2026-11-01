// F6.03 (Editor 9): the Animation window. Dopesheet (one row per animated property, keys as diamonds, events lane), curve editor,
// preview scrubbing and record mode on the open scene (editor/core AnimPreview), property menu built from reflection.
// --ui-event verbs: animwindow[:off] | animclip:<Assets/..clip.json> | animroot:<object> | animprop:<object>|<Component.field.part> |
// animkey:<object>|<Component.field.part>|<tick>|<value> | animevent:<tick>|<function>[|<int>|<float>|<string>] | animrecord:on|off |
// animpreview:on|off | animscrub:<tick> | animplay:on|off | animcurves:on|off | animsave | animundo | animredo | animset:fps|length|loop=<v> |
// animsel:<curve>|<key> | animdump | animvalue:<object>|<Component.field.part>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>

#include "app.h"
#include "panel_registry.h"
#include "rensga/core/animator_system.h"
#include "rensga/editor/anim_clip_doc.h"
#include "rensga/editor/anim_preview.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct AnimWin {
  bool show = false;
  AnimClipDoc clip;
  AnimPreview preview;
  std::string rel;             // Assets/..clip.json
  std::string rootGuid;
  bool wantPreview = true;
  bool playing = false;        // preview playback
  double time = 0;             // ticks
  bool curves = false;         // curve editor instead of the dopesheet
  int selCurve = -1, selKey = -1, selEvent = -1;
  float pxPerTick = 16;
  float scroll = 0;            // first visible tick
  int dragCurve = -1, dragKey = -1, dragEvent = -1;
  bool scrubbing = false;
  char fnBuf[64] = "Footstep";
  int evInt = 0;
  float evFloat = 0;
  char evStr[32] = {};
  std::string status;
  std::string docPath;         // scene the preview was started for
};
AnimWin& W() { static AnimWin* w = new AnimWin(); return *w; }

SceneObject* objectByGuid(App& a, const std::string& guid) {
  for (const SceneObject& o : a.doc.scene().objects) if (o.guid == guid) return const_cast<SceneObject*>(&o);
  return nullptr;
}

bool hasType(const SceneObject& o, const char* t) {
  for (const SceneComponent& c : o.components) if (c.type == t) return true;
  return false;
}

// Subtree of the animated object, root first, with the relative path of each member.
void subtree(App& a, const std::string& rootGuid, std::vector<std::pair<const SceneObject*, std::string>>& out) {
  const Scene& sc = a.doc.scene();
  for (const SceneObject& o : sc.objects) if (o.guid == rootGuid) out.push_back({&o, std::string()});
  for (size_t i = 0; i < out.size(); ++i) {
    const std::string pg = out[i].first->guid, pp = out[i].second;
    for (const SceneObject& o : sc.objects)
      if (o.parentGuid == pg) out.push_back({&o, pp.empty() ? o.name : pp + "/" + o.name});
  }
}

std::string chooseRoot(App& a) {
  const SceneObject* sel = objectByGuid(a, a.doc.selected());
  if (sel) return sel->guid;
  for (const SceneObject& o : a.doc.scene().objects) if (hasType(o, "Animator")) return o.guid;
  return a.doc.scene().objects.empty() ? std::string() : a.doc.scene().objects[0].guid;
}

void stopPreview(App& a) {
  AnimWin& w = W();
  if (!w.preview.active()) return;
  if (a.session.active()) w.preview.forget();
  else w.preview.end(a.doc);
}

bool startPreview(App& a) {
  AnimWin& w = W();
  if (w.preview.active()) return true;
  if (!w.clip.loaded() || a.session.active() || !a.doc.loaded()) return false;
  if (w.rootGuid.empty() || !objectByGuid(a, w.rootGuid)) w.rootGuid = chooseRoot(a);
  std::string err;
  if (!w.preview.begin(a.doc, w.rootGuid, w.clip, err)) { w.status = err; return false; }
  w.docPath = a.doc.path().string();
  w.preview.setTime(w.time);
  return true;
}

bool openClip(App& a, const std::string& rel) {
  AnimWin& w = W();
  stopPreview(a);
  std::string err;
  AnimClipDoc fresh;
  if (!fresh.load(a.project.root / rel, err)) { w.status = err; EditorLog::instance().error("animation: " + err); return false; }
  w.clip = fresh;
  w.rel = rel;
  w.show = true;
  w.time = 0;
  w.selCurve = w.selKey = w.selEvent = -1;
  w.rootGuid = chooseRoot(a);
  w.status.clear();
  if (w.wantPreview) startPreview(a);
  return true;
}

void applyAfterEdit(App&) {
  AnimWin& w = W();
  if (w.preview.active()) w.preview.refresh();
}

std::string fmt(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.3f", v);
  std::string s = b;
  while (s.size() > 1 && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

// Value shown for a new key: the preview's value of the property on that object.
double previewValue(const AnimWin& w, const AnimCurve& c) {
  double v = 0;
  if (w.preview.active() && w.preview.value(c.target, c.property, &v)) return v;
  return AnimClipDoc::evalCurve(c, w.time);
}

std::string curveLabel(const AnimCurve& c) { return (c.target.empty() ? std::string() : c.target + " : ") + c.property; }

// ---- drawing ------------------------------------------------------------------------------------------------------------------

void addKeyHere(App& a, int ci) {
  AnimWin& w = W();
  if (ci < 0 || ci >= static_cast<int>(w.clip.curves().size())) return;
  const AnimCurve& c = w.clip.curves()[ci];
  const u32 tick = static_cast<u32>(w.time + 0.5);
  if (animValueKind(c.property) == AnimValueKind::Guid) return;
  w.clip.setKey(ci, tick, previewValue(w, c));
  applyAfterEdit(a);
}

void drawAddProperty(App& a) {
  AnimWin& w = W();
  if (!ImGui::BeginPopup("##addprop")) return;
  std::vector<std::pair<const SceneObject*, std::string>> objs;
  subtree(a, w.rootGuid, objs);
  for (auto& po : objs) {
    const SceneObject& o = *po.first;
    const std::string label = po.second.empty() ? o.name + " (animated object)" : po.second;
    if (!ImGui::BeginMenu((label + "##o" + o.guid).c_str())) continue;
    std::vector<std::string> types;
    for (const SceneComponent& c : o.components) types.push_back(c.type);
    for (const AnimComponentProps& cp : animatableProperties(types)) {
      if (!ImGui::BeginMenu((cp.component + "##c" + o.guid).c_str())) continue;
      for (const AnimProperty& p : cp.properties) {
        const bool have = w.clip.findCurve(po.second, p.property) >= 0;
        if (ImGui::MenuItem(p.label.c_str(), nullptr, false, !have)) {
          const int ci = w.clip.addCurve(po.second, p.property);
          if (ci >= 0) { AnimCurve c = w.clip.curves()[ci]; w.selCurve = ci; addKeyHere(a, ci); }
          applyAfterEdit(a);
        }
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenu();
  }
  ImGui::EndPopup();
}

void drawToolbarRow(App& a) {
  AnimWin& w = W();
  bool pv = w.preview.active();
  if (ImGui::Checkbox("Preview", &pv)) {
    w.wantPreview = pv;
    if (pv) startPreview(a); else stopPreview(a);
  }
  ImGui::SameLine();
  bool rec = w.preview.recording();
  ImGui::BeginDisabled(!w.preview.active());
  if (rec) ImGui::PushStyleColor(ImGuiCol_Button, brandToken("danger"));
  if (ImGui::Button(rec ? "Recording" : "Record")) w.preview.setRecord(!rec);
  if (rec) ImGui::PopStyleColor();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(w.playing ? "Pause" : "Play")) w.playing = !w.playing;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  float t = static_cast<float>(w.time);
  if (ImGui::DragFloat("##time", &t, 0.1f, 0, static_cast<float>(w.clip.length()), "%.1f")) { w.time = t; w.preview.setTime(w.time); }
  ImGui::SameLine();
  ImGui::TextDisabled("tick  (%.2f s)", w.clip.fps() ? w.time / w.clip.fps() : 0.0);
  ImGui::SameLine();
  int fps = static_cast<int>(w.clip.fps());
  ImGui::SetNextItemWidth(70);
  ImGui::DragInt("fps", &fps, 0.2f, 1, 240);
  if (ImGui::IsItemDeactivatedAfterEdit()) { w.clip.setFps(static_cast<u32>(fps)); applyAfterEdit(a); }
  ImGui::SameLine();
  int len = static_cast<int>(w.clip.length());
  ImGui::SetNextItemWidth(70);
  ImGui::DragInt("length", &len, 0.5f, 1, 100000);
  if (ImGui::IsItemDeactivatedAfterEdit()) { w.clip.setLength(static_cast<u32>(len)); applyAfterEdit(a); }
  ImGui::SameLine();
  bool loop = w.clip.loop();
  if (ImGui::Checkbox("Loop", &loop)) { w.clip.setLoop(loop); applyAfterEdit(a); }
  ImGui::SameLine();
  bool cv = w.curves;
  if (ImGui::Checkbox("Curves", &cv)) w.curves = cv;
  ImGui::SameLine();
  ImGui::BeginDisabled(!w.clip.canUndo());
  if (ImGui::Button("Undo")) { w.clip.undo(); applyAfterEdit(a); }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!w.clip.canRedo());
  if (ImGui::Button("Redo")) { w.clip.redo(); applyAfterEdit(a); }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!w.clip.dirty());
  if (ImGui::Button("Save")) {
    std::string err;
    if (w.clip.save(err)) { EditorLog::instance().info("animation: saved " + w.rel); w.status = "saved"; } else { w.status = err; EditorLog::instance().error("animation: " + err); }
  }
  ImGui::EndDisabled();
}

struct Layout {
  ImVec2 p0, size;  // timeline canvas (screen px)
  float x(double tick, const AnimWin& w) const { return p0.x + static_cast<float>((tick - w.scroll) * w.pxPerTick); }
  double tick(float sx, const AnimWin& w) const { return w.scroll + (sx - p0.x) / w.pxPerTick; }
};

void drawRuler(AnimWin& w, const Layout& L, ImDrawList* dl) {
  const ImU32 line = ImGui::GetColorU32(brandToken("border"));
  const ImU32 txt = ImGui::GetColorU32(brandToken("text-2"));
  dl->AddRectFilled(L.p0, ImVec2(L.p0.x + L.size.x, L.p0.y + 22), ImGui::GetColorU32(brandToken("surface-2")));
  const int step = w.pxPerTick >= 28 ? 1 : (w.pxPerTick >= 12 ? 2 : (w.pxPerTick >= 6 ? 5 : 10));
  const int first = static_cast<int>(w.scroll) / step * step;
  for (int t = first; L.x(t, w) < L.p0.x + L.size.x; t += step) {
    const float x = L.x(t, w);
    if (x < L.p0.x) continue;
    dl->AddLine(ImVec2(x, L.p0.y + 14), ImVec2(x, L.p0.y + 22), line);
    char b[16];
    std::snprintf(b, sizeof b, "%d", t);
    dl->AddText(ImVec2(x + 2, L.p0.y + 1), txt, b);
  }
  // clip end
  const float xe = L.x(w.clip.length(), w);
  dl->AddLine(ImVec2(xe, L.p0.y), ImVec2(xe, L.p0.y + L.size.y), ImGui::GetColorU32(brandToken("muted")));
}

void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 edge) {
  ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
  dl->AddConvexPolyFilled(pts, 4, fill);
  dl->AddPolyline(pts, 4, edge, ImDrawFlags_Closed, 1.0f);
}

void drawDopesheet(App& a, AnimWin& w, const Layout& L) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float rowH = 22, top = L.p0.y + 24;
  const ImU32 border = ImGui::GetColorU32(brandToken("border"));
  const ImU32 keyFill = ImGui::GetColorU32(brandToken("text"));
  const ImU32 keySel = ImGui::GetColorU32(brandToken("primary"));
  const ImU32 evCol = ImGui::GetColorU32(brandToken("info"));
  const int rows = static_cast<int>(w.clip.curves().size());
  // events lane
  const float evY = top;
  dl->AddRectFilled(ImVec2(L.p0.x, evY), ImVec2(L.p0.x + L.size.x, evY + rowH), ImGui::GetColorU32(brandToken("info-bg")));
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool hover = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  for (size_t i = 0; i < w.clip.events().size(); ++i) {
    const AnimEventDef& e = w.clip.events()[i];
    const ImVec2 c(L.x(e.tick, w), evY + rowH * 0.5f);
    diamond(dl, c, 6, static_cast<int>(i) == w.selEvent ? keySel : evCol, border);
    dl->AddText(ImVec2(c.x + 8, evY + 3), ImGui::GetColorU32(brandToken("text-2")), e.function.c_str());
    if (hover && ImGui::IsMouseClicked(0) && std::fabs(mouse.x - c.x) < 7 && std::fabs(mouse.y - c.y) < 9) { w.selEvent = static_cast<int>(i); w.selKey = -1; w.dragEvent = static_cast<int>(i); }
  }
  for (int r = 0; r < rows; ++r) {
    const AnimCurve& c = w.clip.curves()[r];
    const float y = top + rowH * (r + 1);
    if (r == w.selCurve) dl->AddRectFilled(ImVec2(L.p0.x, y), ImVec2(L.p0.x + L.size.x, y + rowH), ImGui::GetColorU32(brandToken("surface-2")));
    dl->AddLine(ImVec2(L.p0.x, y + rowH), ImVec2(L.p0.x + L.size.x, y + rowH), border);
    for (size_t k = 0; k < c.keys.size(); ++k) {
      const ImVec2 p(L.x(c.keys[k].tick, w), y + rowH * 0.5f);
      const bool sel = r == w.selCurve && static_cast<int>(k) == w.selKey;
      diamond(dl, p, 6, sel ? keySel : keyFill, border);
      if (hover && ImGui::IsMouseClicked(0) && std::fabs(mouse.x - p.x) < 7 && std::fabs(mouse.y - p.y) < 9) {
        w.selCurve = r; w.selKey = static_cast<int>(k); w.selEvent = -1; w.dragCurve = r; w.dragKey = static_cast<int>(k);
      }
    }
    // double click in an empty part of the row adds a key at that time
    if (hover && ImGui::IsMouseDoubleClicked(0) && mouse.y >= y && mouse.y < y + rowH && mouse.x > L.p0.x) {
      const u32 tick = static_cast<u32>(std::max(0.0, std::round(L.tick(mouse.x, w))));
      if (!w.clip.hasKeyAt(r, tick) && animValueKind(c.property) != AnimValueKind::Guid) {
        w.clip.setKey(r, tick, AnimClipDoc::evalCurve(c, tick));
        applyAfterEdit(a);
      }
    }
  }
  // key / event drag
  if (w.dragKey >= 0 && ImGui::IsMouseDown(0)) {
    const u32 t = static_cast<u32>(std::max(0.0, std::round(L.tick(mouse.x, w))));
    if (w.dragCurve < rows && w.dragKey < static_cast<int>(w.clip.curves()[w.dragCurve].keys.size()) && w.clip.curves()[w.dragCurve].keys[w.dragKey].tick != t)
      if (w.clip.moveKey(w.dragCurve, w.dragKey, t)) {
        for (size_t k = 0; k < w.clip.curves()[w.dragCurve].keys.size(); ++k) if (w.clip.curves()[w.dragCurve].keys[k].tick == t) { w.dragKey = w.selKey = static_cast<int>(k); }
        applyAfterEdit(a);
      }
  } else if (w.dragEvent >= 0 && ImGui::IsMouseDown(0)) {
    const u32 t = static_cast<u32>(std::max(0.0, std::round(L.tick(mouse.x, w))));
    if (w.dragEvent < static_cast<int>(w.clip.events().size()) && w.clip.events()[w.dragEvent].tick != t) w.clip.moveEvent(w.dragEvent, t);
  }
  if (!ImGui::IsMouseDown(0)) { w.dragKey = w.dragCurve = w.dragEvent = -1; }
}

void drawCurveEditor(App& a, AnimWin& w, const Layout& L) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float top = L.p0.y + 24, h = L.size.y - 28;
  dl->AddRectFilled(ImVec2(L.p0.x, top), ImVec2(L.p0.x + L.size.x, top + h), ImGui::GetColorU32(brandToken("surface")));
  // value range of the selected curve (all curves when none is selected)
  double lo = 1e30, hi = -1e30;
  const auto& cs = w.clip.curves();
  for (size_t i = 0; i < cs.size(); ++i) {
    if (w.selCurve >= 0 && static_cast<int>(i) != w.selCurve) continue;
    for (double t = 0; t <= w.clip.length(); t += std::max(1.0, w.clip.length() / 64.0)) { const double v = AnimClipDoc::evalCurve(cs[i], t); lo = std::min(lo, v); hi = std::max(hi, v); }
    for (const AnimKey& k : cs[i].keys) { lo = std::min(lo, k.value); hi = std::max(hi, k.value); }
  }
  if (lo > hi) { lo = 0; hi = 1; }
  if (hi - lo < 1e-6) { lo -= 1; hi += 1; }
  const double pad = (hi - lo) * 0.1;
  lo -= pad; hi += pad;
  auto Y = [&](double v) { return top + h - static_cast<float>((v - lo) / (hi - lo)) * h; };
  const ImU32 grid = ImGui::GetColorU32(brandToken("border"));
  for (int g = 0; g <= 4; ++g) {
    const double v = lo + (hi - lo) * g / 4.0;
    const float y = Y(v);
    dl->AddLine(ImVec2(L.p0.x, y), ImVec2(L.p0.x + L.size.x, y), grid);
    dl->AddText(ImVec2(L.p0.x + 2, y - 14), ImGui::GetColorU32(brandToken("muted")), fmt(v).c_str());
  }
  static const char* palette[] = {"primary", "info", "ok", "warn", "danger"};
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool hover = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  for (size_t i = 0; i < cs.size(); ++i) {
    const ImU32 col = ImGui::GetColorU32(brandToken(palette[i % 5]));
    const float thick = static_cast<int>(i) == w.selCurve ? 2.5f : 1.2f;
    ImVec2 prev(0, 0);
    bool first = true;
    for (float sx = L.p0.x; sx <= L.p0.x + L.size.x; sx += 3.0f) {
      const ImVec2 p(sx, Y(AnimClipDoc::evalCurve(cs[i], L.tick(sx, w))));
      if (!first) dl->AddLine(prev, p, col, thick);
      prev = p;
      first = false;
    }
    for (size_t k = 0; k < cs[i].keys.size(); ++k) {
      const ImVec2 p(L.x(cs[i].keys[k].tick, w), Y(cs[i].keys[k].value));
      const bool sel = static_cast<int>(i) == w.selCurve && static_cast<int>(k) == w.selKey;
      diamond(dl, p, sel ? 6.0f : 4.5f, sel ? ImGui::GetColorU32(brandToken("text")) : col, grid);
      if (hover && ImGui::IsMouseClicked(0) && std::fabs(mouse.x - p.x) < 7 && std::fabs(mouse.y - p.y) < 7) { w.selCurve = static_cast<int>(i); w.selKey = static_cast<int>(k); w.dragCurve = static_cast<int>(i); w.dragKey = static_cast<int>(k); }
    }
  }
  if (w.dragKey >= 0 && ImGui::IsMouseDown(0) && w.dragCurve < static_cast<int>(cs.size()) && w.dragKey < static_cast<int>(cs[w.dragCurve].keys.size())) {
    const double v = lo + (hi - lo) * (1.0 - (mouse.y - top) / h);
    const u32 t = static_cast<u32>(std::max(0.0, std::round(L.tick(mouse.x, w))));
    const AnimKey k = cs[w.dragCurve].keys[w.dragKey];
    if (t != k.tick) { if (w.clip.moveKey(w.dragCurve, w.dragKey, t)) { for (size_t q = 0; q < cs[w.dragCurve].keys.size(); ++q) if (cs[w.dragCurve].keys[q].tick == t) w.dragKey = w.selKey = static_cast<int>(q); } }
    if (std::fabs(v - cs[w.dragCurve].keys[w.dragKey].value) > 1e-9) { w.clip.setKey(w.dragCurve, cs[w.dragCurve].keys[w.dragKey].tick, v); }
    applyAfterEdit(a);
  }
  if (!ImGui::IsMouseDown(0)) w.dragKey = w.dragCurve = -1;
}

void drawKeyInspector(App& a, AnimWin& w) {
  const auto& cs = w.clip.curves();
  if (w.selCurve >= 0 && w.selCurve < static_cast<int>(cs.size()) && w.selKey >= 0 && w.selKey < static_cast<int>(cs[w.selCurve].keys.size())) {
    const AnimCurve& c = cs[w.selCurve];
    const AnimKey k = c.keys[w.selKey];
    ImGui::Text("Key  %s", curveLabel(c).c_str());
    int tick = static_cast<int>(k.tick);
    ImGui::SetNextItemWidth(70);
    ImGui::DragInt("tick", &tick, 0.2f, 0, 100000);
    if (ImGui::IsItemDeactivatedAfterEdit() && tick != static_cast<int>(k.tick)) { w.clip.moveKey(w.selCurve, w.selKey, static_cast<u32>(tick)); applyAfterEdit(a); }
    ImGui::SameLine();
    if (animValueKind(c.property) == AnimValueKind::Guid) {
      ImGui::Text("guid %s", k.guid.c_str());
    } else {
      float v = static_cast<float>(k.value);
      ImGui::SetNextItemWidth(100);
      if (ImGui::DragFloat("value", &v, 0.05f)) { w.clip.setKey(w.selCurve, k.tick, v); applyAfterEdit(a); }
      const char* modes[] = {"linear", "step", "smooth"};
      int mi = c.interp == "step" ? 1 : (c.interp == "smooth" ? 2 : 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90);
      if (ImGui::Combo("interp", &mi, modes, 3)) { w.clip.setInterp(w.selCurve, modes[mi]); applyAfterEdit(a); }
      if (c.interp == "smooth") {
        float in = static_cast<float>(k.inTangent), out = static_cast<float>(k.outTangent);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        bool ch = ImGui::DragFloat("in", &in, 0.05f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ch = ImGui::DragFloat("out", &out, 0.05f) || ch;
        if (ch) { w.clip.setTangents(w.selCurve, w.selKey, in, out); applyAfterEdit(a); }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete key")) { w.clip.removeKey(w.selCurve, w.selKey); w.selKey = -1; applyAfterEdit(a); }
  } else if (w.selEvent >= 0 && w.selEvent < static_cast<int>(w.clip.events().size())) {
    AnimEventDef e = w.clip.events()[w.selEvent];
    ImGui::Text("Event");
    ImGui::SameLine();
    char fn[64];
    std::snprintf(fn, sizeof fn, "%s", e.function.c_str());
    ImGui::SetNextItemWidth(140);
    bool ch = ImGui::InputText("function", fn, sizeof fn, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    int iv = e.intValue;
    ImGui::DragInt("int", &iv);
    ch = ch || ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    float fv = static_cast<float>(e.floatValue);
    ImGui::DragFloat("float", &fv, 0.05f);
    ch = ch || ImGui::IsItemDeactivatedAfterEdit();
    if (ch) { e.function = fn; e.intValue = iv; e.floatValue = fv; w.clip.setEvent(w.selEvent, e); }
    ImGui::SameLine();
    if (ImGui::Button("Delete event")) { w.clip.removeEvent(w.selEvent); w.selEvent = -1; }
  } else {
    ImGui::TextDisabled("Select a key or an event. Double-click a row to add a key. Alt-drag the ruler to scrub; mouse wheel zooms.");
  }
}

void drawAnimationWindow(App& a) {
  AnimWin& w = W();
  // Housekeeping before drawing: a preview does not survive Play or another scene.
  if (w.preview.active() && (a.session.active() || !a.doc.loaded() || a.doc.path().string() != w.docPath)) stopPreview(a);
  if (!a.loaded || !w.show) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(1180, 400), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 40, vp->WorkPos.y + vp->WorkSize.y - 430), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Animation", &w.show)) { ImGui::End(); return; }
  if (!w.show) stopPreview(a);
  if (!w.clip.loaded()) {
    ImGui::TextWrapped("No clip is open. Create a Property Clip in the Project window (Create > Property Clip) and double-click it, or pick one:");
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(a.project.root / "Assets", ec)) {
      const std::string n = e.path().filename().string();
      if (n.size() > 10 && n.compare(n.size() - 10, 10, ".clip.json") == 0 && ImGui::Selectable(fs::relative(e.path(), a.project.root).generic_string().c_str()))
        openClip(a, fs::relative(e.path(), a.project.root).generic_string());
    }
    ImGui::End();
    return;
  }
  // preview playback
  if (w.playing && w.preview.active()) {
    w.time += ImGui::GetIO().DeltaTime * w.clip.fps();
    const double len = w.clip.length();
    if (w.time >= len) w.time = w.clip.loop() && len > 0 ? std::fmod(w.time, len) : len;
    w.preview.setTime(w.time);
  }
  ImGui::Text("%s%s", w.rel.c_str(), w.clip.dirty() ? " *" : "");
  ImGui::SameLine();
  if (const SceneObject* r = objectByGuid(a, w.rootGuid)) {
    ImGui::TextDisabled("  animating  %s", r->name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Use selection")) {
      const bool was = w.preview.active();
      stopPreview(a);
      w.rootGuid = chooseRoot(a);
      if (was) startPreview(a);
    }
  }
  if (w.preview.recording()) { ImGui::SameLine(); ImGui::TextColored(brandToken("danger"), "  [REC] edits in the Inspector or Scene view become keys at tick %d", static_cast<int>(w.time + 0.5)); }
  drawToolbarRow(a);
  ImGui::Separator();
  const float sideW = 300;
  const float bottomH = 54;
  ImGui::BeginChild("##props", ImVec2(sideW, -bottomH), ImGuiChildFlags_Borders);
  const ImVec2 top0 = ImGui::GetCursorScreenPos();  // the timeline child starts at the same height: its ruler is 22 px, then the events lane
  if (ImGui::Button("Add Property")) ImGui::OpenPopup("##addprop");
  drawAddProperty(a);
  const float rowH = 22;
  const ImVec2 base(top0.x, top0.y + 24);
  // rows are aligned with the dopesheet rows: events lane first, then one row per curve
  ImGui::GetWindowDrawList()->AddText(ImVec2(base.x + 4, base.y + 4), ImGui::GetColorU32(brandToken("info")), "Events");
  for (size_t i = 0; i < w.clip.curves().size(); ++i) {
    const AnimCurve& c = w.clip.curves()[i];
    const ImVec2 rp(base.x, base.y + rowH * (i + 1) + 1);
    ImGui::SetCursorScreenPos(rp);
    const bool sel = static_cast<int>(i) == w.selCurve;
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable("##row", sel, 0, ImVec2(sideW - 70, rowH - 2))) { w.selCurve = static_cast<int>(i); w.selKey = -1; w.selEvent = -1; }
    ImGui::SetCursorScreenPos(ImVec2(rp.x + 4, rp.y + 3));
    ImGui::TextUnformatted(curveLabel(c).c_str());
    ImGui::SameLine(sideW - 62);
    if (ImGui::SmallButton("+")) addKeyHere(a, static_cast<int>(i));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a key at the playhead with the current value");
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) { w.clip.removeCurve(static_cast<int>(i)); w.selCurve = -1; w.selKey = -1; applyAfterEdit(a); ImGui::PopID(); break; }
    ImGui::PopID();
  }
  ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + rowH * (w.clip.curves().size() + 1) + 8));
  ImGui::Dummy(ImVec2(1, 1));
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##timeline", ImVec2(0, -bottomH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  Layout L;
  L.p0 = ImGui::GetCursorScreenPos();
  L.size = ImGui::GetContentRegionAvail();
  // The two lists share the first row (the events lane) offset; the dopesheet draws its own header row at the same height as the toolbar of the list.
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(L.p0, ImVec2(L.p0.x + L.size.x, L.p0.y + L.size.y), true);
  drawRuler(w, L, dl);
  if (w.curves) drawCurveEditor(a, w, L); else drawDopesheet(a, w, L);
  // playhead
  const float px = L.x(w.time, w);
  dl->AddLine(ImVec2(px, L.p0.y), ImVec2(px, L.p0.y + L.size.y), ImGui::GetColorU32(brandToken("primary")), 2.0f);
  dl->AddTriangleFilled(ImVec2(px - 6, L.p0.y), ImVec2(px + 6, L.p0.y), ImVec2(px, L.p0.y + 10), ImGui::GetColorU32(brandToken("primary")));
  dl->PopClipRect();
  // scrubbing: click or drag on the ruler
  const ImVec2 m = ImGui::GetIO().MousePos;
  const bool inRuler = m.x >= L.p0.x && m.x <= L.p0.x + L.size.x && m.y >= L.p0.y && m.y <= L.p0.y + 22 && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  if (inRuler && ImGui::IsMouseClicked(0)) w.scrubbing = true;
  if (w.scrubbing) {
    if (ImGui::IsMouseDown(0)) {
      w.time = std::max(0.0, std::min<double>(w.clip.length(), std::round(L.tick(m.x, w))));
      w.preview.setTime(w.time);
    } else {
      w.scrubbing = false;
    }
  }
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().MouseWheel != 0 && !ImGui::GetIO().WantTextInput) {
    w.pxPerTick = std::max(3.0f, std::min(60.0f, w.pxPerTick * (ImGui::GetIO().MouseWheel > 0 ? 1.15f : 0.87f)));
  }
  // events: right-click the lane to add one at the playhead
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(1) && m.y > L.p0.y + 24 && m.y < L.p0.y + 46) ImGui::OpenPopup("##addevent");
  if (ImGui::BeginPopup("##addevent")) {
    ImGui::TextUnformatted("Animation event at the playhead");
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("function", w.fnBuf, sizeof w.fnBuf);
    ImGui::SetNextItemWidth(80);
    ImGui::DragInt("int", &w.evInt);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::DragFloat("float", &w.evFloat, 0.05f);
    if (ImGui::Button("Add event") && w.fnBuf[0]) {
      w.selEvent = w.clip.addEvent(static_cast<u32>(w.time + 0.5), w.fnBuf, w.evInt, w.evFloat, w.evStr);
      w.selCurve = w.selKey = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::EndChild();
  ImGui::Separator();
  drawKeyInspector(a, w);
  if (!w.status.empty()) { ImGui::SameLine(); ImGui::TextColored(brandToken("warn"), "  %s", w.status.c_str()); }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) { w.clip.undo(); applyAfterEdit(a); }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) { w.clip.redo(); applyAfterEdit(a); }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
      if (w.selKey >= 0 && w.selCurve >= 0) { w.clip.removeKey(w.selCurve, w.selKey); w.selKey = -1; applyAfterEdit(a); }
      else if (w.selEvent >= 0) { w.clip.removeEvent(w.selEvent); w.selEvent = -1; }
    }
  }
  ImGui::End();
}

// ---- --ui-event verbs ---------------------------------------------------------------------------------------------------------------

std::vector<std::string> splitBar(const std::string& s) {
  std::vector<std::string> out;
  size_t p = 0;
  while (true) {
    const size_t q = s.find('|', p);
    out.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
    if (q == std::string::npos) break;
    p = q + 1;
  }
  return out;
}

const SceneObject* byName(App& a, const std::string& name) {
  for (const SceneObject& o : a.doc.scene().objects) if (o.name == name) return &o;
  return nullptr;
}

// "Hero" = the animated object itself, "Hero/Arm" a child path below it; the first part may also be the root's name.
bool targetOf(App& a, const std::string& object, std::string& target) {
  AnimWin& w = W();
  target.clear();
  const SceneObject* root = objectByGuid(a, w.rootGuid);
  if (!root) return false;
  if (object.empty() || object == root->name || object == ".") return true;
  if (object.compare(0, root->name.size() + 1, root->name + "/") == 0) { target = object.substr(root->name.size() + 1); return true; }
  target = object;
  return true;
}

bool animHook(App& a, const std::string& act, const std::string& arg) {
  AnimWin& w = W();
  auto say = [&](const std::string& s) { std::printf("anim: %s\n", s.c_str()); std::fflush(stdout); };
  if (act == "animwindow") { w.show = arg != "off"; if (!w.show) stopPreview(a); say(std::string("window ") + (w.show ? "open" : "closed")); return true; }
  if (act == "animclip") {
    const bool ok = openClip(a, arg);
    say(ok ? "clip " + arg + " fps=" + std::to_string(w.clip.fps()) + " length=" + std::to_string(w.clip.length()) + " curves=" + std::to_string(w.clip.curves().size()) +
                 " events=" + std::to_string(w.clip.events().size()) + " preview=" + (w.preview.active() ? "on" : "off") : "cannot open " + arg + ": " + w.status);
    return true;
  }
  if (act == "animroot") {
    const SceneObject* o = byName(a, arg);
    if (!o) { say("no object " + arg); return true; }
    const bool was = w.preview.active();
    stopPreview(a);
    w.rootGuid = o->guid;
    if (was) startPreview(a);
    say("root " + arg);
    return true;
  }
  if (act == "animpreview") {
    w.wantPreview = arg != "off";
    if (w.wantPreview) startPreview(a); else stopPreview(a);
    say(std::string("preview ") + (w.preview.active() ? "on" : "off") + (w.status.empty() ? "" : " (" + w.status + ")"));
    return true;
  }
  if (act == "animrecord") { w.preview.setRecord(arg != "off" && w.preview.active()); say(std::string("record ") + (w.preview.recording() ? "on" : "off")); return true; }
  if (act == "animscrub") { w.time = std::max(0.0, std::atof(arg.c_str())); w.preview.setTime(w.time); say("time " + fmt(w.time)); return true; }
  if (act == "animplay") { w.playing = arg != "off"; return true; }
  if (act == "animcurves") { w.curves = arg != "off"; return true; }
  if (act == "animsave") { std::string err; const bool ok = w.clip.save(err); say(ok ? "saved " + w.rel : "save failed: " + err); return true; }
  if (act == "animundo") { w.clip.undo(); applyAfterEdit(a); say("undo"); return true; }
  if (act == "animredo") { w.clip.redo(); applyAfterEdit(a); say("redo"); return true; }
  if (act == "animset") {
    const size_t e = arg.find('=');
    const std::string k = arg.substr(0, e), v = e == std::string::npos ? "" : arg.substr(e + 1);
    if (k == "fps") w.clip.setFps(static_cast<u32>(std::atoi(v.c_str())));
    else if (k == "length") w.clip.setLength(static_cast<u32>(std::atoi(v.c_str())));
    else if (k == "loop") w.clip.setLoop(v == "1" || v == "true");
    applyAfterEdit(a);
    say("set " + arg);
    return true;
  }
  if (act == "animsel") {
    const auto p = splitBar(arg);
    w.selCurve = p.size() > 0 ? std::atoi(p[0].c_str()) : -1;
    w.selKey = p.size() > 1 ? std::atoi(p[1].c_str()) : -1;
    w.selEvent = -1;
    return true;
  }
  if (act == "animprop" || act == "animkey") {
    const auto p = splitBar(arg);
    if (p.size() < 2 || (act == "animkey" && p.size() < 4)) { say("usage: " + act + (act == "animkey" ? ":<object>|<Component.field.part>|<tick>|<value>" : ":<object>|<Component.field.part>")); return true; }
    std::string target;
    if (!targetOf(a, p[0], target)) { say("no animated object (open a clip with animclip first)"); return true; }
    const int c = w.clip.addCurve(target, p[1]);
    if (c < 0) { say("cannot animate " + p[1]); return true; }
    w.selCurve = c;
    if (act == "animkey") {
      bool ok;
      if (animValueKind(p[1]) == AnimValueKind::Guid) ok = w.clip.setKeyGuid(c, static_cast<u32>(std::atoi(p[2].c_str())), p[3]);
      else ok = w.clip.setKey(c, static_cast<u32>(std::atoi(p[2].c_str())), p[3] == "true" ? 1.0 : (p[3] == "false" ? 0.0 : std::atof(p[3].c_str())));
      say(std::string(ok ? "key " : "key failed ") + p[1] + " tick=" + p[2] + " value=" + p[3]);
    } else {
      say("curve " + p[1]);
    }
    applyAfterEdit(a);
    return true;
  }
  if (act == "animevent") {
    const auto p = splitBar(arg);
    if (p.size() < 2) { say("usage: animevent:<tick>|<function>[|<int>|<float>|<string>]"); return true; }
    const int e = w.clip.addEvent(static_cast<u32>(std::atoi(p[0].c_str())), p[1], p.size() > 2 ? std::atoi(p[2].c_str()) : 0, p.size() > 3 ? std::atof(p[3].c_str()) : 0, p.size() > 4 ? p[4] : "");
    say(e >= 0 ? "event " + p[1] + " tick=" + p[0] : "event failed");
    return true;
  }
  if (act == "animvalue") {
    const auto p = splitBar(arg);
    std::string target;
    double v = 0;
    if (p.size() >= 2 && targetOf(a, p[0], target) && w.preview.value(target, p[1], &v)) say("value " + p[0] + " " + p[1] + " = " + fmt(v));
    else say("value " + arg + " unavailable");
    return true;
  }
  if (act == "animdump") {
    std::string line = "clip=" + w.rel + " fps=" + std::to_string(w.clip.fps()) + " length=" + std::to_string(w.clip.length()) + " loop=" + (w.clip.loop() ? "1" : "0") +
                       " curves=" + std::to_string(w.clip.curves().size()) + " events=" + std::to_string(w.clip.events().size()) + " tick=" + fmt(w.time) +
                       " preview=" + (w.preview.active() ? "on" : "off") + " record=" + (w.preview.recording() ? "on" : "off") + " dirty=" + (w.clip.dirty() ? "1" : "0") +
                       " keysRecorded=" + std::to_string(w.preview.keysRecorded());
    say(line);
    for (size_t i = 0; i < w.clip.curves().size(); ++i) {
      const AnimCurve& c = w.clip.curves()[i];
      std::string keys;
      for (const AnimKey& k : c.keys) keys += " " + std::to_string(k.tick) + ":" + (animValueKind(c.property) == AnimValueKind::Guid ? k.guid : fmt(k.value));
      double v = 0;
      const bool have = w.preview.active() && w.preview.value(c.target, c.property, &v);
      say("  curve " + std::to_string(i) + " " + curveLabel(c) + " interp=" + c.interp + " keys=" + std::to_string(c.keys.size()) + keys + (have ? " now=" + fmt(v) : ""));
    }
    for (size_t i = 0; i < w.clip.events().size(); ++i) {
      const AnimEventDef& e = w.clip.events()[i];
      say("  event " + std::to_string(i) + " tick=" + std::to_string(e.tick) + " " + e.function + " int=" + std::to_string(e.intValue) + " float=" + fmt(e.floatValue));
    }
    return true;
  }
  return false;
}

bool animHolds(App&, const std::string&) { return false; }

}  // namespace

RENSGA_EDITOR_PANEL(animation_window, "Animation", drawAnimationWindow, animHook, animHolds, 160, 125, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
