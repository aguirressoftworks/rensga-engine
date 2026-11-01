#include <cmath>
#include <cstring>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <new>
#include <set>

#include <imgui_internal.h>

#include "app.h"
#include "panel_registry.h"
#include "panels/asset_ui.h"
#include "panels/drawers/drawers.h"
#include "panels/prefab_ui.h"
#include "panels/preview_state.h"
#include "extensions/ext_host.h"  // F6 D3
#include "panels/tile_tools.h"
#include "rensga/core/reflect.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/reflect_method.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

void drawValidation(App& a);  // below

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

// F5.08: the target's field rules (module + missing features + ConsoleTarget "fieldRules"), cached by ConsoleTarget.json mtime.
const InspectorRules& inspectorRules(App& a, const std::string& targetName) {
  static std::string cachedFor;
  static InspectorRules cached;
  std::error_code ec;
  auto mt = std::filesystem::last_write_time(a.project.root / "ProjectSettings" / "ConsoleTarget.json", ec);
  const std::string key = a.project.root.string() + "|" + a.target + "|" + std::to_string(ec ? 0 : mt.time_since_epoch().count());
  if (key != cachedFor) {
    cachedFor = key;
    cached = effectiveInspectorRules(a.targetModule(), consoleTargetFor(a.project.root), a.project.root, targetName);
  }
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
                   const std::vector<std::string>& sel, const std::string& primary, bool multi, const InspectorRules& rules) {
  const bool playing = a.doc.playing();
  const SceneObject* po = a.doc.find(primary);
  if (!po) return;
  const ComponentRef& pr = cr.refs[0];
  const TypeInfo* ti = TypeRegistry::instance().find(cr.type.c_str());
  std::string grey = greyReason(cr.type, allowed, targetName);
  const std::string featureWhy = fieldGreyReason(rules, cr.type, "*");  // F5.08: the whole component is unusable on this target
  if (grey.empty() && !featureWhy.empty()) grey = featureWhy;
  const bool missing = ti == nullptr;

  ImGui::PushID(rowIdx);
  if (!grey.empty() || missing) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
  std::string title = missing ? cr.type + "  (missing script)" : cr.type;
  if (const MemberInfo* pm = a.doc.memberInfo(pr.guid))  // F6.01: a component the instance added to its prefab
    if ((pm->isRoot || !pm->added) && pr.index < static_cast<int>(pm->comps.size()) && !pm->comps[static_cast<size_t>(pr.index)].addedId.empty()) title += "  (added)";
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
    const MemberInfo* pmi = a.doc.memberInfo(pr.guid);
    const bool prefabOrder = pmi && (pmi->isRoot || !pmi->added);  // the prefab owns the component order
    ImGui::BeginDisabled(playing || pr.index == 0 || prefabOrder);
    if (ImGui::MenuItem("Move Up"))
      withUndoStep(a, "Move", [&] { OpResult l; l.ok = true; for (const ComponentRef& r : cr.refs) l = moveComponentBy(a.doc, r.guid, r.index, -1); return l; });
    ImGui::EndDisabled();
    ImGui::BeginDisabled(playing || prefabOrder || pr.index + 1 >= static_cast<int>(po->components.size()));
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
      if (!featureWhy.empty()) ImGui::TextWrapped("[!] %s", featureWhy.c_str());
      else ImGui::TextWrapped("Not supported by %s: the cook will reject this component.", targetName.c_str());
      ImGui::PopStyleColor();
    }
    if (ti) {
      // reflected fields; a greyed component is shown disabled
      ImGui::BeginDisabled(!grey.empty());
      bool inTable = false;
      for (u32 i = 0; i < ti->fieldCount; ++i) {
        const FieldDesc& f = ti->fields[i];
        if (f.hideInInspector || extensionReplacesFields(cr.type)) continue;  // F6 D3: RENSGA_CUSTOM_INSPECTOR_REPLACE draws instead
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
        const std::string fieldWhy = fieldGreyReason(rules, cr.type, f.name);
        if (!fieldWhy.empty()) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
        // F6.01: a field the instance overrides gets a bar and the info colour; right-click the name for Apply / Revert
        const bool ovr = !multi && a.doc.isOverridden(pr.guid, pr.index, f.name);
        if (ovr) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("info"));
        row(prettyFieldName(f.name).c_str(), fieldWhy.empty() ? f.tooltip : fieldWhy.c_str());
        if (ovr) {
          const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
          ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x - 7, mn.y), ImVec2(mn.x - 4, mx.y), ImGui::GetColorU32(brandToken("info")));
          if (ImGui::IsItemHovered()) {
            const json pv = a.doc.prefabValue(pr.guid, pr.index, f.name);
            ImGui::SetTooltip("Overridden on this instance (prefab value: %s)", pv.is_null() ? "default" : pv.dump().c_str());
          }
          if (ImGui::BeginPopupContextItem("ovctx")) {
            if (ImGui::MenuItem("Apply to Prefab")) prefabDefer([g = pr.guid, ci = pr.index, fn = std::string(f.name)](App& app) { app.doc.applyField(g, ci, fn); });
            if (ImGui::MenuItem("Revert")) prefabDefer([g = pr.guid, ci = pr.index, fn = std::string(f.name)](App& app) { app.doc.revertField(g, ci, fn); });
            ImGui::EndPopup();
          }
          ImGui::PopStyleColor();
        }
        if (!fieldWhy.empty()) ImGui::PopStyleColor();
        ImGui::BeginDisabled(!fieldWhy.empty());
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
        ImGui::EndDisabled();
        if (!fieldWhy.empty() && fieldWhy != featureWhy) {  // the reason, in words (not colour alone), under the greyed field
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(1);
          ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
          ImGui::TextWrapped("[!] %s", fieldWhy.c_str());
          ImGui::PopStyleColor();
        }
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

// ---- F5.07 / F5.08: the scene cooked in the target's form, and the validation list --------------------------------------------
PreviewOptions& previewOptions() {
  static PreviewOptions o;
  return o;
}

namespace {
struct ScenePreviewCache {
  ConsolePreview cp;
  std::string key, err, root;
  double checkedAt = -100;
  bool have = false;
};
ScenePreviewCache g_sp;
}  // namespace

const ConsolePreview* scenePreview(App& a, bool force, std::string* err) {
  ScenePreviewCache& c = g_sp;
  if (err) err->clear();
  if (!a.loaded || !a.doc.loaded() || a.target.empty() || !CookerPluginRegistry::instance().find(a.target)) {
    if (err) *err = "this project has no cooker plugin for its target";
    return nullptr;
  }
  const double now = ImGui::GetTime();
  if (force || now - c.checkedAt > 0.5) {
    c.checkedAt = now;
    const std::string key = a.project.root.string() + "|" + a.target + "|" + a.doc.path().string() + "|" + a.doc.document().dump();
    if (force || key != c.key || !c.have) {
      c.key = key;
      c.have = true;
      c.root = a.project.root.string();
      if (!cookConsolePreview(a.project.root, a.target, a.doc.path(), a.doc.document(), c.cp)) c.err = c.cp.error;
      else c.err.clear();
    }
  }
  if (!c.cp.ok) {
    if (err) *err = c.err;
    return nullptr;
  }
  return &c.cp;
}

void drawValidation(App& a) {
  ImGui::Spacing();
  const ConsolePreview* cp = scenePreview(a);
  int nw = 0, ne = 0;
  if (cp) { nw = cp->count(ValidationItem::Warning); ne = cp->count(ValidationItem::Error); }
  char title[96];
  std::snprintf(title, sizeof title, "Scene validation  (%d error%s, %d warning%s)###validation", ne, ne == 1 ? "" : "s", nw, nw == 1 ? "" : "s");
  if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) return;
  std::string err;
  if (!cp) {
    scenePreview(a, false, &err);
    ImGui::TextWrapped("%s", err.empty() ? "No validation for this project." : err.c_str());
    return;
  }
  if (ImGui::SmallButton("Validate again")) scenePreview(a, true);
  ImGui::SameLine();
  ImGui::TextDisabled("%s cook, %.0f ms", cp->target.c_str(), cp->ms);
  if (cp->items.empty()) {
    ImGui::TextColored(brandToken("ok"), "[ok]");
    ImGui::SameLine();
    ImGui::TextUnformatted("The scene fits the target's limits.");
    return;
  }
  int idx = 0;
  for (const ValidationItem& it : cp->items) {
    ImGui::PushID(idx++);
    const bool bad = it.severity == ValidationItem::Error;
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken(bad ? "danger" : "warn"));
    ImGui::TextUnformatted(bad ? "[x]" : "[!]");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", it.category.c_str());
    ImGui::SameLine();
    const bool clickable = !it.objectGuid.empty() && a.doc.find(it.objectGuid);
    const std::string label = (it.objectName.empty() ? it.text : it.objectName + " " + it.text);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    if (clickable) {
      if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Select %s", it.objectName.c_str()); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
      if (ImGui::IsItemClicked()) a.doc.select(it.objectGuid);
    } else if (!it.asset.empty() && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", it.asset.c_str());
    }
    ImGui::PopID();
  }
}

bool previewHook(App& a, const std::string& act, const std::string& arg) {
  PreviewOptions& po = previewOptions();
  if (act == "preview") {  // preview:dither=1,frame=1,safe=0,color=16,textures=1,lights=1,bars=1 | preview:reset
    po.initialized = true;
    if (arg == "reset") { po = PreviewOptions(); return true; }
    size_t pos = 0;
    while (pos < arg.size()) {
      size_t e = arg.find(',', pos);
      const std::string kv = arg.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
      const size_t eq = kv.find('=');
      const std::string k = kv.substr(0, eq), v = eq == std::string::npos ? "1" : kv.substr(eq + 1);
      const int n = std::atoi(v.c_str());
      if (k == "dither") po.dither = n != 0;
      else if (k == "frame") po.deviceFrame = n != 0;
      else if (k == "safe") po.safeArea = n != 0;
      else if (k == "color") po.quantize = n <= 16;
      else if (k == "textures") po.consoleTextures = n != 0;
      else if (k == "lights") po.lightLimit = n != 0;
      else if (k == "bars") po.bars = n != 0;
      else std::printf("preview: unknown key '%s'\n", k.c_str());
      if (e == std::string::npos) break;
      pos = e + 1;
    }
    return true;
  }
  if (act == "previewdump") {
    const PreviewRules pr = effectivePreviewRules(a.targetModule(), consoleTargetFor(a.project.root));
    std::printf("preview: %dx%d colorBits=%d dither=%d maxLights=%d | quantize=%d dither=%d frame=%d safe=%d textures=%d lightLimit=%d bars=%d\n", pr.width, pr.height,
                pr.colorBits, pr.dither, pr.maxLightsPerObject, po.quantize, po.dither, po.deviceFrame, po.safeArea, po.consoleTextures, po.lightLimit, po.bars);
    return true;
  }
  if (act == "validate" || act == "validatesel") {
    std::string err;
    const ConsolePreview* cp = scenePreview(a, true, &err);
    if (!cp) { std::printf("validate: unavailable (%s)\n", err.c_str()); return true; }
    if (act == "validatesel") {
      const size_t n = static_cast<size_t>(std::atoi(arg.c_str()));
      if (n < cp->items.size() && !cp->items[n].objectGuid.empty()) {
        a.doc.select(cp->items[n].objectGuid);
        std::printf("validate: selected %s\n", cp->items[n].objectName.c_str());
      } else std::printf("validate: finding %zu names no object\n", n);
      return true;
    }
    std::printf("validate: %zu finding(s) errors=%d warnings=%d target=%s packBytes=%llu triangles=%llu textureBytes=%llu\n", cp->items.size(),
                cp->count(ValidationItem::Error), cp->count(ValidationItem::Warning), cp->target.c_str(), static_cast<unsigned long long>(cp->packBytes),
                static_cast<unsigned long long>(cp->triangles), static_cast<unsigned long long>(cp->textureBytes));
    int i = 0;
    for (const ValidationItem& it : cp->items)
      std::printf("validate-item: %d %s %s object=%s %s\n", i++, it.severity == ValidationItem::Error ? "error" : "warning", it.category.c_str(),
                  it.objectName.empty() ? "-" : it.objectName.c_str(), it.text.c_str());
    return true;
  }
  return false;
}

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
    drawValidation(a);
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
  if (!multi) drawPrefabInspectorHeader(a, guid);  // F6.01: prefab asset, Open, Select, overrides menu

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
  const InspectorRules& frules = inspectorRules(a, targetName);
  for (size_t i = 0; i < rows.size(); ++i) drawComponent(a, rows[i], static_cast<int>(i), allowed, targetName, sel, guid, multi, frules);

  ImGui::Spacing();
  ImGui::BeginDisabled(a.doc.playing());
  if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("addcomp");
  ImGui::EndDisabled();
  drawAddComponentPopup(a, "addcomp", sel, allowed, targetName);
  drawValidation(a);
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
  if (previewHook(a, act, arg)) return true;
  if (act == "inspscroll") { g_scrollTo = static_cast<float>(std::atof(arg.c_str())); return true; }
  if (act == "undo") { a.doc.undo(); return true; }
  if (act == "redo") { a.doc.redo(); return true; }
  return false;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(inspector, "Inspector", drawInspector, inspectorHook, nullptr, 100, 80, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
