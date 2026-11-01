// F6.01 (Editor 6): prefab edit mode, breadcrumbs, the Inspector's prefab strip and the --ui-event prefab* verbs.
#include "panels/prefab_ui.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "panel_registry.h"
#include "panels/asset_ui.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

PrefabUi& prefabUi() {
  static PrefabUi* u = new PrefabUi();
  return *u;
}

namespace {

bool saveStageHook(const ordered_json& j, std::string& err) {
  PrefabUi& u = prefabUi();
  if (u.stages.empty()) { err = "no prefab is open"; return false; }
  PrefabStageLevel& lv = u.stages.back();
  PrefabAsset out;
  if (!stageToPrefab(j, out, err, &lv.ids)) return false;
  out.guid = lv.prefabGuid;
  for (const PrefabObject& o : out.objects) {
    if (o.nestedAsset.empty()) continue;
    const std::string cyc = u.lib.wouldCycle(lv.prefabGuid, o.nestedAsset);
    if (!cyc.empty()) { err = cyc; return false; }
  }
  if (out.isVariant()) {
    const std::string cyc = u.lib.wouldCycle(lv.prefabGuid, out.variantBase);
    if (!cyc.empty()) { err = cyc; return false; }
  }
  return u.lib.save(lv.prefabGuid, out, err);
}

std::string objectGuidByName(App& a, const std::string& spec) {
  std::string name = spec;
  int nth = 1;
  const size_t hash = spec.rfind('#');
  if (hash != std::string::npos) { name = spec.substr(0, hash); nth = std::max(1, std::atoi(spec.c_str() + hash + 1)); }
  for (const SceneObject& o : a.doc.scene().objects)
    if (o.name == name && --nth == 0) return o.guid;
  return std::string();
}

std::string prefabGuidFromSpec(App& a, const std::string& spec) {
  prefabEnsure(a);
  PrefabUi& u = prefabUi();
  if (const AssetEntry* e = assetUi().sync.db().findPath(spec)) return e->kind == AssetKind::Prefab ? e->guid : std::string();
  for (const std::string& g : u.lib.allGuids())
    if (u.lib.nameOf(g) == spec || u.lib.relOf(g) == spec) return g;
  return std::string();
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> v;
  std::string cur;
  for (char c : s) { if (c == sep) { v.push_back(cur); cur.clear(); } else cur += c; }
  v.push_back(cur);
  return v;
}

}  // namespace

void prefabDefer(std::function<void(App&)> fn) { prefabUi().deferred.push_back(std::move(fn)); }

void prefabEnsure(App& a) {
  PrefabUi& u = prefabUi();
  if (!a.loaded) return;
  assetUiTick(a);  // opens the asset database for the project
  if (u.root != a.project.root) {
    u.root = a.project.root;
    u.stages.clear();
    u.lib.open(&assetUi().sync.db());
  }
  if (a.doc.prefabLibrary() != &u.lib) a.doc.setPrefabLibrary(&u.lib);
}

bool prefabInStage(const App&) { return !prefabUi().stages.empty(); }

std::string prefabStageName(const App&) {
  PrefabUi& u = prefabUi();
  return u.stages.empty() ? std::string() : u.lib.nameOf(u.stages.back().prefabGuid);
}

bool prefabSaveStage(App& a) {
  PrefabUi& u = prefabUi();
  if (u.stages.empty()) return false;
  std::string err;
  if (!a.doc.save(err)) {
    u.error = err;
    EditorLog::instance().error("prefab: " + err);
    return false;
  }
  u.error.clear();
  return true;
}

static bool loadStage(App& a, const std::string& prefabGuid) {
  PrefabUi& u = prefabUi();
  PrefabAsset asset;
  std::string err;
  if (!u.lib.get(prefabGuid, asset, err)) {
    u.error = err;
    EditorLog::instance().warn("prefab: " + err);
    return false;
  }
  a.doc.setSaveHook(saveStageHook);
  a.doc.restore(u.lib.pathOf(prefabGuid), prefabToStage(asset), false);
  const std::vector<std::string> roots = a.doc.childrenOf("");
  a.doc.select(roots.empty() ? std::string() : roots.front());
  return true;
}

bool prefabOpen(App& a, const std::string& prefabGuid) {
  prefabEnsure(a);
  PrefabUi& u = prefabUi();
  if (!a.doc.loaded()) return false;
  if (a.session.active()) stopPlayInEditor(a);
  if (!u.stages.empty()) {
    if (u.stages.back().prefabGuid == prefabGuid) return true;
    if (a.doc.dirty() && !prefabSaveStage(a)) return false;
  } else {
    u.mainPath = a.doc.path();
    u.mainDoc = a.doc.document();
    u.mainDirty = a.doc.dirty();
    u.mainSelection = a.doc.selected();
  }
  const size_t before = u.stages.size();
  u.stages.push_back({prefabGuid, StageIds()});
  if (!loadStage(a, prefabGuid)) {
    u.stages.resize(before);
    if (before == 0) { a.doc.setSaveHook(nullptr); a.doc.restore(u.mainPath, u.mainDoc, u.mainDirty); }
    return false;
  }
  a.sv.needFrame = true;
  EditorLog::instance().info("prefab: opened " + u.lib.relOf(prefabGuid));
  return true;
}

bool prefabClose(App& a, int keepLevels, const std::string& mode) {
  PrefabUi& u = prefabUi();
  if (u.stages.empty()) return false;
  keepLevels = std::max(0, std::min<int>(keepLevels, static_cast<int>(u.stages.size()) - 1));
  while (static_cast<int>(u.stages.size()) > keepLevels) {
    if (a.doc.dirty()) {
      if (mode == "discard") {
      } else if (u.autoSave || mode == "save") {
        if (!prefabSaveStage(a)) return false;
      } else {
        u.error = "unsaved changes: Save or Discard first";
        std::printf("prefab: close refused: unsaved changes\n");
        return false;
      }
    }
    u.stages.pop_back();
    if (u.stages.empty()) {
      a.doc.setSaveHook(nullptr);
      a.doc.restore(u.mainPath, u.mainDoc, u.mainDirty);
      a.doc.select(u.mainSelection);
      u.mainDoc = ordered_json();
    } else if (!loadStage(a, u.stages.back().prefabGuid)) {
      return false;
    }
  }
  u.error.clear();
  return true;
}

std::string prefabCreateFromObject(App& a, const std::string& objectGuid, const fs::path& folder) {
  prefabEnsure(a);
  std::string err;
  std::error_code ec;
  fs::create_directories(folder, ec);
  const std::string pg = a.doc.createPrefab(objectGuid, folder, err);
  if (pg.empty()) {
    EditorLog::instance().warn("prefab: " + err);
    prefabUi().error = err;
    return pg;
  }
  a.assetsStamp = -100;
  EditorLog::instance().info("prefab: created " + prefabUi().lib.relOf(pg));
  return pg;
}

bool prefabInstantiateFromAsset(App& a, const std::string& assetGuid, const std::string& targetObject) {
  prefabEnsure(a);
  PrefabUi& u = prefabUi();
  if (!a.doc.loaded()) return false;
  if (!u.stages.empty()) {
    const std::string cyc = u.lib.wouldCycle(u.stages.back().prefabGuid, assetGuid);
    if (!cyc.empty()) {
      u.error = cyc;
      EditorLog::instance().error("prefab: " + cyc);
      std::printf("prefab: refused: %s\n", cyc.c_str());
      return false;
    }
  }
  const std::string g = a.doc.instantiatePrefab(assetGuid, targetObject);
  if (g.empty()) { EditorLog::instance().warn("prefab: cannot instantiate " + u.lib.relOf(assetGuid)); return false; }
  return true;
}

// ---------------------------------------------------------------- drawing

void drawPrefabBreadcrumbs(App& a) {
  PrefabUi& u = prefabUi();
  if (u.stages.empty()) return;
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("info"));
  ImGui::TextUnformatted("Prefab Mode");
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (ImGui::SmallButton((u.mainPath.stem().string() + "##crumb_scene").c_str())) prefabDefer([](App& app) { prefabClose(app, 0); });
  for (size_t i = 0; i < u.stages.size(); ++i) {
    ImGui::SameLine();
    ImGui::TextDisabled(">");
    ImGui::SameLine();
    const std::string label = u.lib.nameOf(u.stages[i].prefabGuid) + "##crumb" + std::to_string(i);
    if (i + 1 < u.stages.size()) { if (ImGui::SmallButton(label.c_str())) prefabDefer([keep = static_cast<int>(i) + 1](App& app) { prefabClose(app, keep); }); }
    else ImGui::TextUnformatted(u.lib.nameOf(u.stages[i].prefabGuid).c_str());
  }
  ImGui::Checkbox("Auto Save", &u.autoSave);
  ImGui::SameLine();
  ImGui::BeginDisabled(!a.doc.dirty());
  if (ImGui::SmallButton("Save")) prefabDefer([](App& app) { prefabSaveStage(app); });
  ImGui::SameLine();
  if (ImGui::SmallButton("Discard")) {
    const std::string g = u.stages.back().prefabGuid;
    prefabDefer([g](App& app) { loadStage(app, g); });
  }
  ImGui::EndDisabled();
  if (a.doc.dirty() && !u.autoSave) { ImGui::SameLine(); ImGui::TextColored(brandToken("warn"), "[!] unsaved"); }
  if (!u.error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
    ImGui::TextWrapped("[x] %s", u.error.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::Separator();
}

void drawPrefabInspectorHeader(App& a, const std::string& guid) {
  PrefabUi& u = prefabUi();
  const std::string root = a.doc.instanceRootOf(guid);
  if (root.empty()) return;
  const std::string asset = a.doc.prefabOf(guid);
  ImGui::PushID("prefabstrip");
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("info"));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Prefab");
  ImGui::SameLine();
  ImGui::TextUnformatted(u.lib.nameOf(asset).c_str());
  ImGui::PopStyleColor();
  const int n = a.doc.overrideCount(root);
  ImGui::SameLine();
  if (ImGui::SmallButton("Open")) prefabDefer([asset](App& app) { prefabOpen(app, asset); });
  ImGui::SameLine();
  if (ImGui::SmallButton("Select")) prefabDefer([asset](App& app) { selectAsset(app, asset); });
  ImGui::SameLine();
  char ov[48];
  std::snprintf(ov, sizeof ov, "Overrides (%d)##ovmenu", n);
  if (ImGui::SmallButton(ov)) ImGui::OpenPopup("ovpop");
  if (ImGui::BeginPopup("ovpop")) {
    ImGui::BeginDisabled(n == 0);
    if (ImGui::MenuItem("Apply All to Prefab")) prefabDefer([root](App& app) { app.doc.applyInstance(root); });
    if (ImGui::MenuItem("Revert All")) prefabDefer([root](App& app) { app.doc.revertInstance(root); });
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!a.doc.hasObjectOverrides(guid));
    if (ImGui::MenuItem("Apply This Object to Prefab")) prefabDefer([guid](App& app) { app.doc.applyObject(guid); });
    if (ImGui::MenuItem("Revert This Object")) prefabDefer([guid](App& app) { app.doc.revertObject(guid); });
    ImGui::EndDisabled();
    ImGui::Separator();
    if (a.doc.isInstanceRoot(guid) && ImGui::MenuItem("Unpack Prefab")) prefabDefer([root](App& app) { app.doc.unpackPrefab(root); });
    ImGui::EndPopup();
  }
  ImGui::PopID();
  ImGui::Separator();
}

static void drawPrefabSync(App& a) {
  if (!a.loaded) return;
  prefabEnsure(a);
  PrefabUi& u = prefabUi();
  if (!u.deferred.empty()) {
    std::vector<std::function<void(App&)>> todo;
    todo.swap(u.deferred);
    for (auto& fn : todo) fn(a);
  }
  u.lib.poll(ImGui::GetTime());
  a.doc.refreshPrefabs();
  if (!u.stages.empty() && u.autoSave && a.doc.dirty() && !a.doc.undoStack().inGroup() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) prefabSaveStage(a);
}

// ---------------------------------------------------------------- verbs


bool prefabUiHook(App& a, const std::string& act, const std::string& arg) {
  if (act.compare(0, 6, "prefab") != 0) return false;
  prefabEnsure(a);
  PrefabUi& u = prefabUi();
  const std::vector<std::string> p = split(arg, '|');
  auto objArg = [&](size_t i) { return i < p.size() ? objectGuidByName(a, p[i]) : std::string(); };

  if (act == "prefabcreate" || act == "prefabvariant") {  // prefabcreate:<object>[|<folder>]
    const std::string g = objArg(0);
    std::string folder = p.size() > 1 && !p[1].empty() ? p[1] : (assetUi().folder.empty() ? "Assets" : assetUi().folder);
    if (g.empty()) { std::printf("prefab: create failed: no object '%s'\n", p.empty() ? "" : p[0].c_str()); return true; }
    const std::string pg = prefabCreateFromObject(a, g, a.project.root / folder);
    if (pg.empty()) std::printf("prefab: create failed: %s\n", u.error.c_str());
    else std::printf("prefab: %s %s guid=%s\n", act == "prefabvariant" ? "variant" : "created", u.lib.relOf(pg).c_str(), pg.c_str());
  } else if (act == "prefabinstance") {  // prefabinstance:<prefab path or name>[|<parent object>[|x,y,z]]
    const std::string pg = p.empty() ? std::string() : prefabGuidFromSpec(a, p[0]);
    if (pg.empty()) { std::printf("prefab: instance failed: no prefab '%s'\n", p.empty() ? "" : p[0].c_str()); return true; }
    const std::string parent = p.size() > 1 && !p[1].empty() ? objArg(1) : std::string();
    float pos[3] = {0, 0, 0};
    bool hasPos = false;
    if (p.size() > 2) {
      const std::vector<std::string> xyz = split(p[2], ',');
      if (xyz.size() == 3) { for (int i = 0; i < 3; ++i) pos[i] = static_cast<float>(std::atof(xyz[static_cast<size_t>(i)].c_str())); hasPos = true; }
    }
    if (!u.stages.empty()) {
      const std::string cyc = u.lib.wouldCycle(u.stages.back().prefabGuid, pg);
      if (!cyc.empty()) { std::printf("prefab: refused: %s\n", cyc.c_str()); u.error = cyc; return true; }
    }
    const std::string g = a.doc.instantiatePrefab(pg, parent, hasPos ? pos : nullptr);
    if (g.empty()) std::printf("prefab: instance failed\n");
    else std::printf("prefab: instance %s name=%s\n", g.c_str(), a.doc.find(g) ? a.doc.find(g)->name.c_str() : "?");
  } else if (act == "prefabapply" || act == "prefabrevert") {   // <object>  |  <object>|object  |  <object>|<component index>|<field>
    const bool apply = act == "prefabapply";
    const std::string g = objArg(0);
    if (g.empty()) { std::printf("prefab: %s failed: no object\n", apply ? "apply" : "revert"); return true; }
    bool ok = false;
    if (p.size() >= 3) ok = apply ? a.doc.applyField(g, std::atoi(p[1].c_str()), p[2]) : a.doc.revertField(g, std::atoi(p[1].c_str()), p[2]);
    else if (p.size() == 2 && p[1] == "object") ok = apply ? a.doc.applyObject(g) : a.doc.revertObject(g);
    else { const std::string r = a.doc.instanceRootOf(g); ok = !r.empty() && (apply ? a.doc.applyInstance(r) : a.doc.revertInstance(r)); }
    std::printf("prefab: %s %s\n", apply ? "apply" : "revert", ok ? "ok" : "nothing to do");
  } else if (act == "prefabset") {   // prefabset:<object>|<component index>|<field>|<json>
    const std::string g = objArg(0);
    json v = p.size() >= 4 ? json::parse(arg.substr(arg.find('|', arg.find('|', arg.find('|') + 1) + 1) + 1), nullptr, false) : json();
    const bool ok = !g.empty() && p.size() >= 4 && !v.is_discarded() && a.doc.setField(g, std::atoi(p[1].c_str()), p[2], v);
    std::printf("prefab: set %s\n", ok ? "ok" : "failed");
  } else if (act == "prefabselect") {  // prefabselect:<name>[#n]: the n-th object of that name in hierarchy order (select: takes the last)
    const std::string g = objArg(0);
    a.doc.select(g);
    std::printf("prefab: select %s\n", g.empty() ? "failed" : a.doc.find(g)->name.c_str());
  } else if (act == "prefabunpack") {
    const std::string g = objArg(0);
    std::printf("prefab: unpack %s\n", !g.empty() && a.doc.unpackPrefab(g) ? "ok" : "failed");
  } else if (act == "prefabopen") {
    const std::string pg = p.empty() ? std::string() : prefabGuidFromSpec(a, p[0]);
    std::printf("prefab: open %s\n", !pg.empty() && prefabOpen(a, pg) ? "ok" : "failed");
  } else if (act == "prefabclose") {   // prefabclose[:save|discard]
    std::printf("prefab: close %s\n", prefabClose(a, 0, arg) ? "ok" : "failed");
  } else if (act == "prefabback") {    // prefabback:<levels to keep open>
    std::printf("prefab: back %s\n", prefabClose(a, std::atoi(arg.c_str()), "") ? "ok" : "failed");
  } else if (act == "prefabsave") {
    std::printf("prefab: save %s\n", prefabSaveStage(a) ? "ok" : "failed");
  } else if (act == "prefabautosave") {
    u.autoSave = arg != "off" && arg != "0";
    std::printf("prefab: autosave %s\n", u.autoSave ? "on" : "off");
  } else if (act == "prefabpoll") {    // pick up a prefab file changed on disk now
    u.lib.poll(1e9, 0);
    std::printf("prefab: poll %s\n", a.doc.refreshPrefabs() ? "refreshed" : "unchanged");
  } else if (act == "prefabfield") {   // prefabfield:<object>|<component index>|<field>
    const std::string g = objArg(0);
    const SceneObject* o = a.doc.find(g);
    const int ci = p.size() > 1 ? std::atoi(p[1].c_str()) : 0;
    if (!o || p.size() < 3 || ci < 0 || ci >= static_cast<int>(o->components.size())) { std::printf("prefab: field failed\n"); return true; }
    const json& f = o->components[static_cast<size_t>(ci)].fields;
    const json cur = f.contains(p[2]) ? f[p[2]] : json();
    std::printf("prefab: field %s %s.%s = %s overridden=%d prefab=%s\n", o->name.c_str(), o->components[static_cast<size_t>(ci)].type.c_str(), p[2].c_str(),
                cur.is_null() ? "null" : cur.dump().c_str(), a.doc.isOverridden(g, ci, p[2]) ? 1 : 0,
                a.doc.prefabValue(g, ci, p[2]).is_null() ? "null" : a.doc.prefabValue(g, ci, p[2]).dump().c_str());
  } else if (act == "prefabdump") {    // prefabdump[:<object>]
    if (!u.stages.empty()) {
      std::string crumbs = u.mainPath.stem().string();
      for (const PrefabStageLevel& l : u.stages) crumbs += " > " + u.lib.nameOf(l.prefabGuid);
      std::printf("prefab: stage level=%zu prefab=%s dirty=%d autosave=%d breadcrumbs=\"%s\" objects=%zu\n", u.stages.size(),
                  u.lib.nameOf(u.stages.back().prefabGuid).c_str(), a.doc.dirty() ? 1 : 0, u.autoSave ? 1 : 0, crumbs.c_str(), a.doc.scene().objects.size());
    } else {
      std::printf("prefab: scene objects=%zu prefabs=%zu problems=%zu\n", a.doc.scene().objects.size(), u.lib.allGuids().size(), a.doc.prefabProblems().size());
    }
    for (const std::string& pr : a.doc.prefabProblems()) std::printf("prefab: problem %s\n", pr.c_str());
    const std::string g = p.empty() || p[0].empty() ? a.doc.selected() : objArg(0);
    const std::string root = a.doc.instanceRootOf(g);
    if (!root.empty()) {
      const SceneObject* ro = a.doc.find(root);
      int members = 0;
      for (const SceneObject& o : a.doc.scene().objects) if (a.doc.instanceRootOf(o.guid) == root) ++members;
      std::printf("prefab: instance %s asset=%s overrides=%d members=%d\n", ro ? ro->name.c_str() : "?", u.lib.relOf(a.doc.prefabOf(root)).c_str(), a.doc.overrideCount(root), members);
      for (const ordered_json& oj : a.doc.document()["objects"]) {
        if (oj.value("guid", "") != root) continue;
        for (const char* k : {"overrides", "addedComponents", "removedComponents", "removedObjects", "addedObjects"})
          if (oj.contains(k)) for (const ordered_json& e : oj[k]) std::printf("prefab:   %s %s\n", k, e.dump().c_str());
      }
    } else if (!g.empty()) {
      std::printf("prefab: %s is not part of a prefab instance\n", a.doc.find(g) ? a.doc.find(g)->name.c_str() : g.c_str());
    }
  } else return false;
  std::fflush(stdout);
  return true;
}

RENSGA_EDITOR_PANEL(prefab_sync, "Prefabs", drawPrefabSync, prefabUiHook, nullptr, 1, 25, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
