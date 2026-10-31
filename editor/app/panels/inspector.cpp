#include <cmath>
#include <cstring>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <fstream>
#include <functional>
#include <map>
#include <new>
#include <set>

#include <imgui_internal.h>

#include "app.h"
#include "panels/asset_ui.h"
#include "panels/drawers/drawers.h"
#include "panels/tile_tools.h"
#include "rensga/core/reflect.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/reflect_method.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

namespace {

using nlohmann::json;

float g_scrollTo = -1;  // --ui-event inspscroll:<y>

void touch(App& a) {
  if (!inspectorGesture()) { a.doc.beginGesture(); inspectorGesture() = true; }
}

// Component types the target accepts: its inspector rules plus the project's own ConsoleTarget list (scripts).
std::set<std::string> allowedComponents(App& a) {
  static std::string cachedFor;
  static std::set<std::string> cached;
  std::error_code ec;
  auto mt = std::filesystem::last_write_time(a.project.root / "ProjectSettings" / "ConsoleTarget.json", ec);
  const std::string key = a.project.root.string() + "|" + a.target + "|" + std::to_string(ec ? 0 : mt.time_since_epoch().count());
  if (key == cachedFor) return cached;
  cachedFor = key;
  cached.clear();
  if (const TargetModule* tm = a.targetModule())
    for (const std::string& n : tm->inspectorRules(consoleTargetFor(a.project.root)).allowedComponents) cached.insert(n);
  try {
    std::ifstream in(a.project.root / "ProjectSettings" / "ConsoleTarget.json");
    json j = json::parse(in);
    for (const json& n : j.value("allowedComponents", json::array())) cached.insert(n.get<std::string>());
  } catch (...) {}
  return cached;
}

bool beginFields(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchProp)) return false;
  ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch, 0.40f);
  ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch, 0.60f);
  return true;
}

void row(const char* label, const char* tooltip = nullptr) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}


// One row of the component list: the type, and the same component on every selected object.
struct CompRow {
  std::string type;
  std::vector<ComponentRef> refs;  // refs[0] is the primary object's
};

std::string withUndoStep(App& a, const char* what, const std::function<OpResult()>& fn) {
  (void)what;
  a.doc.beginGesture();
  OpResult r = fn();
  a.doc.endGesture();
  return r.ok ? std::string() : r.reason;
}

void drawComponent(App& a, const CompRow& cr, int rowIdx, const std::set<std::string>& allowed, const std::string& targetName,
                   const std::vector<std::string>& sel, const std::string& primary, bool multi) {
  const bool playing = a.doc.playing();
  const SceneObject* po = a.doc.find(primary);
  if (!po) return;
  const ComponentRef& pr = cr.refs[0];
  const TypeInfo* ti = TypeRegistry::instance().find(cr.type.c_str());
  const std::string grey = greyReason(cr.type, allowed, targetName);
  const bool missing = ti == nullptr;

  ImGui::PushID(rowIdx);
  if (!grey.empty() || missing) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
  std::string title = missing ? cr.type + "  (missing script)" : cr.type;
  if (multi) title += "  (" + std::to_string(cr.refs.size()) + ")";
  bool open = ImGui::CollapsingHeader((title + "###hdr").c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
  if (!grey.empty() || missing) ImGui::PopStyleColor();

  // header menu: the "..." button at the right edge and a right click on the header
  ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 24);
  if (ImGui::SmallButton("...")) ImGui::OpenPopup("cmenu");
  bool menu = ImGui::BeginPopup("cmenu");
  if (!menu) menu = ImGui::BeginPopupContextItem("cmenu2");
  if (menu) {
    const OpResult dummy;
    (void)dummy;
    std::string removeWhy = playing ? "Stop play mode to change components" : std::string();
    for (const ComponentRef& r : cr.refs)
      if (removeWhy.empty()) { const SceneObject* o = a.doc.find(r.guid); if (o) removeWhy = removeBlockReason(*o, r.index); }
    ImGui::BeginDisabled(playing || missing);
    if (ImGui::MenuItem("Reset")) {
      std::string why = withUndoStep(a, "Reset", [&] {
        OpResult last; last.ok = true;
        for (const ComponentRef& r : cr.refs) { last = resetComponent(a.doc, r.guid, r.index); if (!last) break; }
        return last;
      });
      if (!why.empty()) a.status = why;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!removeWhy.empty());
    bool doRemove = ImGui::MenuItem("Remove Component");
    ImGui::EndDisabled();
    if (!removeWhy.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", removeWhy.c_str());
    if (doRemove) {
      std::string why = withUndoStep(a, "Remove", [&] {
        OpResult last; last.ok = true;
        for (const ComponentRef& r : cr.refs) { last = removeComponentFrom(a.doc, r.guid, r.index); if (!last) break; }
        return last;
      });
      if (!why.empty()) a.status = why;
    }
    ImGui::Separator();
    ImGui::BeginDisabled(playing || pr.index == 0);
    if (ImGui::MenuItem("Move Up"))
      withUndoStep(a, "Move", [&] { OpResult l; l.ok = true; for (const ComponentRef& r : cr.refs) l = moveComponentBy(a.doc, r.guid, r.index, -1); return l; });
    ImGui::EndDisabled();
    ImGui::BeginDisabled(playing || pr.index + 1 >= static_cast<int>(po->components.size()));
    if (ImGui::MenuItem("Move Down"))
      withUndoStep(a, "Move", [&] { OpResult l; l.ok = true; for (const ComponentRef& r : cr.refs) l = moveComponentBy(a.doc, r.guid, r.index, 1); return l; });
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Component Values")) copyComponentValues(a.doc, pr.guid, pr.index);
    const ComponentClipboard& cb = componentClipboard();
    const bool canPaste = !playing && cb.has && cb.type == cr.type;
    ImGui::BeginDisabled(!canPaste);
    if (ImGui::MenuItem("Paste Component Values"))
      withUndoStep(a, "Paste", [&] { OpResult l; l.ok = true; for (const ComponentRef& r : cr.refs) l = pasteComponentValues(a.doc, r.guid, r.index); return l; });
    ImGui::EndDisabled();
    if (!canPaste && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", !cb.has ? "Copy Component Values first" : playing ? "Stop play mode first" : ("The clipboard holds " + cb.type + " values").c_str());
    if (ti && ti->contextMenuCount > 0) {
      ImGui::Separator();
      for (u32 i = 0; i < ti->contextMenuCount; ++i) {
        ImGui::BeginDisabled(playing);
        if (ImGui::MenuItem(ti->contextMenus[i].label)) {
          const std::string method = ti->contextMenus[i].method;
          withUndoStep(a, "Context", [&] { OpResult l; l.ok = true; for (const ComponentRef& r : cr.refs) l = runContextMenu(a.doc, r.guid, r.index, method); return l; });
        }
        ImGui::EndDisabled();
      }
    }
    ImGui::EndPopup();
  }

  if (open) {
    if (!grey.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
      ImGui::TextWrapped("Not supported by %s: the cook will reject this component.", targetName.c_str());
      ImGui::PopStyleColor();
    }
    if (ti) {
      // reflected fields; a greyed component is shown disabled
      ImGui::BeginDisabled(!grey.empty());
      bool inTable = false;
      for (u32 i = 0; i < ti->fieldCount; ++i) {
        const FieldDesc& f = ti->fields[i];
        if (f.hideInInspector) continue;
        if (f.header) {
          if (inTable) { ImGui::EndTable(); inTable = false; }
          ImGui::SeparatorText(f.header);
        }
        if (f.space) {
          if (inTable) { ImGui::EndTable(); inTable = false; }
          ImGui::Dummy(ImVec2(0, static_cast<float>(f.space)));
        }
        if (!inTable) inTable = beginFields("##f");
        if (!inTable) break;
        row(prettyFieldName(f.name).c_str(), f.tooltip);
        ImGui::PushID(f.name);
        DrawCtx ctx{a, f, pr.guid + "/" + std::to_string(pr.index) + "/" + f.name, pr.guid, multi && fieldMixed(a.doc, cr.refs, f.name)};
        const json cur = fieldValue(a.doc, pr, f.name);
        json nv;
        FieldDrawer draw = drawerFor(ti->name, f);
        if (draw && draw(ctx, cur, nv)) {
          touch(a);
          editFields(a.doc, cr.refs, f.name, nv);
        }
        if (f.tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.tooltip);
        ImGui::PopID();
      }
      if (inTable) ImGui::EndTable();
      drawComponentExtras(a, cr.type, cr.refs, multi);  // F4.08: sprite picker, Open Tile Palette
      ImGui::EndDisabled();
    } else {
      // The scene JSON keeps the component and saves it unchanged (the script may be compiling, renamed or removed).
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
      ImGui::TextWrapped("Missing script (%s)", cr.type.c_str());
      ImGui::PopStyleColor();
      const SceneComponent& c = po->components[pr.index];
      if (beginFields("##raw")) {
        for (auto it = c.fields.begin(); it != c.fields.end(); ++it) {
          row(it.key().c_str());
          ImGui::BeginDisabled();
          ImGui::TextUnformatted(it.value().dump().c_str());
          ImGui::EndDisabled();
        }
        ImGui::EndTable();
      }
    }
  }
  (void)sel;
  ImGui::PopID();
}

}  // namespace

void drawInspector(App& a) {
  if (inspectorGesture() && !ImGui::IsAnyItemActive()) { a.doc.endGesture(); inspectorGesture() = false; }
  if (!a.showInspector) return;
  registerBuiltinDrawers();
  if (!ImGui::Begin("Inspector", nullptr)) { ImGui::End(); return; }
  if (g_scrollTo >= 0) { ImGui::SetScrollY(g_scrollTo); g_scrollTo = -1; }
  if (drawAssetInspector(a)) { ImGui::End(); return; }  // F3.02: a Project asset is selected
  const std::vector<std::string> sel = a.doc.selection();
  const SceneObject* o = a.doc.find(a.doc.selected());
  if (!o) {
    ImGui::TextDisabled("Select an object");
    ImGui::End();
    return;
  }
  const bool multi = sel.size() > 1;
  const std::string guid = o->guid;
  auto allOf = [&](auto get) {  // value of the primary and whether the selection disagrees
    bool mixed = false;
    for (const std::string& g : sel) if (const SceneObject* x = a.doc.find(g)) mixed |= get(*x) != get(*o);
    return mixed;
  };

  // name / active / tag / layer: every edit applies to the whole selection as one undo step
  const bool activeMixed = multi && allOf([](const SceneObject& x) { return x.active; });
  bool active = o->active;
  if (activeMixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
  if (ImGui::Checkbox("##active", &active)) editObjects(a.doc, sel, "active", active);
  if (activeMixed) ImGui::PopItemFlag();
  ImGui::SameLine();
  char name[128];
  const bool nameMixed = multi && allOf([](const SceneObject& x) { return x.name; });
  std::snprintf(name, sizeof name, "%s", nameMixed ? "" : o->name.c_str());
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool entered = ImGui::InputTextWithHint("##name", nameMixed ? mixedText() : "Name", name, sizeof name,
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  if (entered || ImGui::IsItemDeactivatedAfterEdit()) { if (name[0] || !nameMixed) editObjects(a.doc, sel, "name", std::string(name)); }
  if (multi) ImGui::TextDisabled("%d objects selected (edits apply to all)", static_cast<int>(sel.size()));
  else ImGui::TextDisabled("%s", guid.c_str());
  {
    const bool tagMixed = multi && allOf([](const SceneObject& x) { return x.tag; });
    const bool layerMixed = multi && allOf([](const SceneObject& x) { return x.layer; });
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Tag");
    ImGui::SameLine();
    char tag[64];
    std::snprintf(tag, sizeof tag, "%s", tagMixed ? "" : o->tag.c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::InputTextWithHint("##tag", tagMixed ? mixedText() : "Untagged", tag, sizeof tag);
    if (ImGui::IsItemDeactivatedAfterEdit()) editObjects(a.doc, sel, "tag", std::string(tag));
    ImGui::SameLine();
    ImGui::TextUnformatted("Layer");
    ImGui::SameLine();
    int layer = o->layer;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragInt("##layer", &layer, 0.2f, 0, 31, layerMixed ? mixedText() : "%d")) {
      touch(a);
      editObjects(a.doc, sel, "layer", layer);
    }
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen) && beginFields("##xf")) {
    float p[3] = {o->position[0], o->position[1], o->position[2]};
    float q[4] = {o->rotation[0], o->rotation[1], o->rotation[2], o->rotation[3]};
    float s[3] = {o->scale[0], o->scale[1], o->scale[2]};
    auto differs = [&](int part) {
      if (!multi) return false;
      for (const std::string& g : sel) {
        const SceneObject* x = a.doc.find(g);
        if (!x) continue;
        for (int i = 0; i < (part == 1 ? 4 : 3); ++i) {
          const float* mine = part == 0 ? o->position : part == 1 ? o->rotation : o->scale;
          const float* other = part == 0 ? x->position : part == 1 ? x->rotation : x->scale;
          if (mine[i] != other[i]) return true;
        }
      }
      return false;
    };
    int editedPart = -1;
    row("Position");
    if (ImGui::DragFloat3("##p", p, 0.05f, 0, 0, differs(0) ? mixedText() : "%.3g")) editedPart = 0;
    row("Rotation");
    if (eulerWidget(guid + "/T", q, "##rot", differs(1))) editedPart = 1;
    row("Scale");
    if (ImGui::DragFloat3("##s", s, 0.02f, 0, 0, differs(2) ? mixedText() : "%.3g")) editedPart = 2;
    ImGui::EndTable();
    if (editedPart >= 0) {
      touch(a);
      for (const std::string& g : sel) {
        const SceneObject* x = a.doc.find(g);
        if (!x) continue;
        float pp[3] = {x->position[0], x->position[1], x->position[2]};
        float rr[4] = {x->rotation[0], x->rotation[1], x->rotation[2], x->rotation[3]};
        float ss[3] = {x->scale[0], x->scale[1], x->scale[2]};
        if (editedPart == 0) std::memcpy(pp, p, sizeof pp);
        if (editedPart == 1) std::memcpy(rr, q, sizeof rr);
        if (editedPart == 2) std::memcpy(ss, s, sizeof ss);
        a.doc.setTransform(g, pp, rr, ss);
      }
    }
  }

  const std::set<std::string> allowed = allowedComponents(a);
  const TargetModule* tm = a.targetModule();
  const std::string targetName = tm ? std::string(tm->displayName()) : std::string("target");

  std::vector<CompRow> rows;
  if (!multi) {
    for (size_t ci = 0; ci < o->components.size(); ++ci) rows.push_back({o->components[ci].type, {ComponentRef{guid, static_cast<int>(ci)}}});
  } else {
    for (const std::string& t : commonComponentTypes(a.doc, sel)) {
      std::vector<ComponentRef> refs = sameTypeAcross(a.doc, sel, t);
      if (!refs.empty()) rows.push_back({t, refs});
    }
    if (rows.size() < (o->components.size())) ImGui::TextDisabled("Showing components all selected objects share");
  }
  for (size_t i = 0; i < rows.size(); ++i) drawComponent(a, rows[i], static_cast<int>(i), allowed, targetName, sel, guid, multi);

  ImGui::Spacing();
  ImGui::BeginDisabled(a.doc.playing());
  if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("addcomp");
  ImGui::EndDisabled();
  drawAddComponentPopup(a, "addcomp", sel, allowed, targetName);
  // a script dragged from the Project window onto the Inspector goes on the selected objects
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  if (!a.doc.playing() && ImGui::BeginDragDropTargetCustom(win->Rect(), win->ID)) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_SCRIPT")) {
      a.doc.beginGesture();
      for (const std::string& g : sel) addScriptToObject(a, g, static_cast<const char*>(p->Data));
      a.doc.endGesture();
    }
    ImGui::EndDragDropTarget();
  }
  ImGui::End();
}


// --ui-event verbs for the Inspector (documented in AGENTS.md); each runs the same model calls as the UI.
bool inspectorHook(App& a, const std::string& act, const std::string& arg) {
  const std::vector<std::string> sel = a.doc.selection();
  const std::string primary = a.doc.selected();
  auto fail = [&](const std::string& why) { if (!why.empty()) EditorLog::instance().warn("inspector: " + why); return true; };
  auto idxOf = [&](const std::string& s) { return std::atoi(s.c_str()); };
  auto eachSel = [&](const std::function<OpResult(const std::string&)>& fn) {
    a.doc.beginGesture();
    std::string why;
    for (const std::string& g : sel) { OpResult r = fn(g); if (!r) { why = r.reason; break; } }
    a.doc.endGesture();
    return fail(why);
  };
  if (act == "multiselect") {  // multiselect:A|B|C (object names)
    std::vector<std::string> guids;
    size_t pos = 0;
    while (pos <= arg.size()) {
      size_t e = arg.find('|', pos);
      std::string n = arg.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
      for (const SceneObject& o : a.doc.scene().objects) if (o.name == n) { guids.push_back(o.guid); break; }
      if (e == std::string::npos) break;
      pos = e + 1;
    }
    a.doc.setSelection(guids);
    return true;
  }
  if (act == "addcomp") return eachSel([&](const std::string& g) { return addComponentTo(a.doc, g, arg, allowedComponents(a), "target"); });
  if (act == "rmcomp") return eachSel([&](const std::string& g) { return removeComponentFrom(a.doc, g, idxOf(arg)); });
  if (act == "moveup") return eachSel([&](const std::string& g) { return moveComponentBy(a.doc, g, idxOf(arg), -1); });
  if (act == "movedown") return eachSel([&](const std::string& g) { return moveComponentBy(a.doc, g, idxOf(arg), 1); });
  if (act == "resetcomp") return eachSel([&](const std::string& g) { return resetComponent(a.doc, g, idxOf(arg)); });
  if (act == "copycomp") { OpResult r = copyComponentValues(a.doc, primary, idxOf(arg)); return fail(r.reason); }
  if (act == "pastecomp") return eachSel([&](const std::string& g) { return pasteComponentValues(a.doc, g, idxOf(arg)); });
  if (act == "ctxmenu") {  // ctxmenu:<component index>|<method>
    size_t bar = arg.find('|');
    if (bar == std::string::npos) return fail("ctxmenu:<index>|<method>");
    const int ci = idxOf(arg.substr(0, bar));
    const std::string m = arg.substr(bar + 1);
    return eachSel([&](const std::string& g) { return runContextMenu(a.doc, g, ci, m); });
  }
  if (act == "setfield") {  // setfield:<Type>.<field>=<json>, on every selected object that has the type
    size_t dot = arg.find('.'), eq = arg.find('=');
    if (dot == std::string::npos || eq == std::string::npos || eq < dot) return fail("setfield:<Type>.<field>=<json>");
    std::vector<ComponentRef> refs = sameTypeAcross(a.doc, sel, arg.substr(0, dot));
    json v = json::parse(arg.substr(eq + 1), nullptr, false);
    if (refs.empty() || v.is_discarded()) return fail("setfield: no such component on the whole selection, or bad JSON");
    OpResult r = editFields(a.doc, refs, arg.substr(dot + 1, eq - dot - 1), v);
    return fail(r.reason);
  }
  if (act == "setobj") {  // setobj:name|active|tag|layer=<json>
    size_t eq = arg.find('=');
    if (eq == std::string::npos) return fail("setobj:<name|active|tag|layer>=<json>");
    json v = json::parse(arg.substr(eq + 1), nullptr, false);
    if (v.is_discarded()) v = arg.substr(eq + 1);
    OpResult r = editObjects(a.doc, sel, arg.substr(0, eq), v);
    return fail(r.reason);
  }
  if (act == "inspscroll") { g_scrollTo = static_cast<float>(std::atof(arg.c_str())); return true; }
  if (act == "undo") { a.doc.undo(); return true; }
  if (act == "redo") { a.doc.redo(); return true; }
  return false;
}

}  // namespace editor
}  // namespace rensga
