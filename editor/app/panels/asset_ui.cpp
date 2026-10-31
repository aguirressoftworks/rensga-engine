#include "panels/asset_ui.h"
#include "panel_registry.h"

#include <algorithm>
#include <cstdlib>

#include "rensga/core/reflect.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/script_build.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

AssetUi& assetUi() {
  static AssetUi* u = new AssetUi();
  return *u;
}

void selectAsset(App& a, const std::string& guid) {
  AssetUi& u = assetUi();
  u.selGuid = guid;
  a.doc.select("");  // the Inspector shows the asset until an object is picked again
  u.selDocObject = a.doc.selected();
}

static void openFor(App& a) {
  AssetUi& u = assetUi();
  u.sync.stop();
  u.thumbs.clear();
  u.root = a.project.root;
  u.ct = consoleTargetFor(a.project.root);
  u.cookTarget = !a.project.target.empty() ? a.project.target : u.ct.profile;
  u.selGuid.clear();
  u.tab.clear();
  u.pendingGuid.clear();
  u.undo.clear();
  u.redo.clear();
  u.folder = "Assets";
  u.vramKeyGen = -1;
  u.sync.open(a.project.root, u.cookTarget);
  u.sync.syncNow(true);  // creates missing .meta files, follows external moves
  u.sync.start();
}

void assetUiTick(App& a) {
  AssetUi& u = assetUi();
  if (!a.loaded) return;
  if (u.root != a.project.root) openFor(a);
  SyncResult r;
  if (u.sync.pump(r)) {
    a.assetsStamp = -100;
    auto drop = [&](const std::string& rel) {
      if (const AssetEntry* e = u.sync.db().findPath(rel)) u.thumbs.invalidate(e->guid);
    };
    for (const std::string& rel : r.scan.changed) drop(rel);
    for (const std::string& rel : r.reimported) drop(rel);
    for (const auto& m : r.scan.moved) drop(m.second);
    if (!u.selGuid.empty() && !u.sync.db().findGuid(u.selGuid)) u.selGuid.clear();
    u.ct = consoleTargetFor(a.project.root);
  }
  u.thumbs.pump();
}

bool openSceneFile(App& a, const fs::path& file) {
  if (a.doc.dirty()) {
    EditorLog::instance().warn("save the open scene first (Ctrl+S), then open " + file.filename().string());
    return false;
  }
  std::string err;
  SceneDocument next;
  if (!next.load(file, err)) { EditorLog::instance().error("scene load failed: " + err); return false; }
  a.doc = std::move(next);
  a.sv.needFrame = true;
  EditorLog::instance().info("scene opened: " + file.filename().string());
  return true;
}

bool acceptAssetDrop(App& a, const std::string& guid, const std::string& targetObject) {
  AssetUi& u = assetUi();
  EditorLog& log = EditorLog::instance();
  const AssetEntry* e = u.sync.db().findGuid(guid);
  if (!e) { log.warn("drop: " + u.sync.db().describeGuid(guid)); return false; }
  switch (e->kind) {
    case AssetKind::Scene: return openSceneFile(a, e->path);
    case AssetKind::Script: {
      std::string cls;
      for (const ScriptInfo& sc : a.scripts.list)
        if (sc.path == e->path) cls = sc.name;
      const std::string obj = targetObject.empty() ? a.doc.selected() : targetObject;
      if (cls.empty()) { log.warn("drop: " + e->rel + " holds no script class"); return false; }
      if (obj.empty()) { log.warn("drop: select an object first, or drop the script on one"); return false; }
      return addScriptToObject(a, obj, cls);
    }
    case AssetKind::Texture: {
      const std::vector<std::string>& allowed = u.ct.allowedComponents;
      if (!allowed.empty() && std::find(allowed.begin(), allowed.end(), "SpriteRenderer") == allowed.end()) {
        log.warn("drop: the project's target does not allow SpriteRenderer");
        return false;
      }
      const TypeInfo* ti = TypeRegistry::instance().find("SpriteRenderer");
      if (!ti) { log.warn("drop: SpriteRenderer is not registered"); return false; }
      SceneComponent c;
      c.type = "SpriteRenderer";
      c.fields = defaultFieldsFor(*ti);
      c.fields["texture"] = guid;
      std::string name = e->path.stem().string();
      const std::string id = a.doc.createObject(name, "", {c});
      if (id.empty()) return false;
      a.doc.select(id);
      log.info("created '" + name + "' with SpriteRenderer from " + e->rel);
      return true;
    }
    default:
      log.warn("drop: " + e->rel + " (" + assetKindName(e->kind) + ") has no drop action here");
      return false;
  }
}

bool assetUiHook(App& a, const std::string& act, const std::string& arg) {
  AssetUi& u = assetUi();
  if (act == "asset") {  // asset:<project-relative path>
    assetUiTick(a);
    if (const AssetEntry* e = u.sync.db().findPath(arg)) {
      selectAsset(a, e->guid);
      u.folder = fs::path(e->rel).parent_path().generic_string();
      u.search[0] = 0;
    } else EditorLog::instance().warn("asset: not found: " + arg);
  } else if (act == "createmenu") u.openCreateMenu = true;   // F6: Project > Create menu open (screenshot)
  else if (act == "assetcreate") {                      // assetcreate:<Prefab|Material|Animator_Controller|Property_Clip|Data_Asset>[|name]
    const size_t bar = arg.find('|');
    createAssetFromMenu(a, bar == std::string::npos ? arg : arg.substr(0, bar), bar == std::string::npos ? std::string() : arg.substr(bar + 1));
  } else if (act == "assettab") assetInspectSetTab(a, arg);
  else if (act == "assetset") {
    const size_t eq = arg.find('=');
    if (eq == std::string::npos || !assetInspectSetField(a, arg.substr(0, eq), arg.substr(eq + 1))) EditorLog::instance().warn("assetset: bad field or value: " + arg);
  } else if (act == "assetapply") assetInspectApply(a);
  else if (act == "assetrevert") assetInspectRevert(a);
  else if (act == "assetundo") assetInspectUndo(a, false);
  else if (act == "assetredo") assetInspectUndo(a, true);
  else if (act == "vram") u.showVram = true;
  else if (act == "search") std::snprintf(u.search, sizeof u.search, "%s", arg.c_str());
  else if (act == "folder") { u.folder = arg.empty() ? "Assets" : arg; u.search[0] = 0; }
  else if (act == "assetrename" || act == "assetmove") {  // <rel>|<new name> / <rel>|<folder rel>
    const size_t bar = arg.find('|');
    std::string err;
    bool ok = false;
    if (bar != std::string::npos) {
      const fs::path from = a.project.root / arg.substr(0, bar);
      const std::string rest = arg.substr(bar + 1);
      ok = act == "assetrename" ? u.sync.db().rename(from, rest, err) : u.sync.db().move(from, a.project.root / rest, err);
    } else err = "expected <path>|<value>";
    if (!ok) EditorLog::instance().warn(act + ": " + err);
    a.assetsStamp = -100;
  } else if (act == "assetdelete") {
    std::string err;
    if (!u.sync.db().remove(a.project.root / arg, err)) EditorLog::instance().warn("assetdelete: " + err);
    a.assetsStamp = -100;
  } else if (act == "assetdrop") {  // assetdrop:<rel>[|<object name>]: same code as dropping the asset on the Hierarchy
    const size_t bar = arg.find('|');
    const AssetEntry* e = u.sync.db().findPath(bar == std::string::npos ? arg : arg.substr(0, bar));
    std::string obj;
    if (bar != std::string::npos)
      for (const SceneObject& o : a.doc.scene().objects)
        if (o.name == arg.substr(bar + 1)) obj = o.guid;
    if (e) acceptAssetDrop(a, e->guid, obj);
  } else return false;
  return true;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(asset_ui, "Assets (verbs)", nullptr, assetUiHook, nullptr, 0, 100, ::rensga::editor::PanelDraw::None)

}  // namespace editor
}  // namespace rensga
