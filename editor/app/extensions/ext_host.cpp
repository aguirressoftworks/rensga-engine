// F6.07 (Editor 13): builds, loads and hot-reloads the project's Editor/ module and exposes what it registered: dockable windows,
// menu items with shortcuts, custom inspectors. The build runs on a worker thread (ScriptBuildJob, like game scripts); a successful
// build swaps the module between frames, after dropping every registration of the old one. See docs/rensga-engine/editor-extensions.md.
//
// --ui-event verbs (spaces in names are written as '_'):
//   extbuild            build the Editor/ module now and load it (blocking; headless tests)
//   extwait             hold the queue until no extension build is running
//   extdump             print "ext: module loaded=.. generation=.. inspectors=.. windows=.. menus=.. building=.. failed=.."
//   extwindow:<title>[|off]   open (or close) a registered window
//   extmenu:<Menu/Path>       run a registered menu item
//   extstatus[:off]           open the "Editor Extensions" status window
//   extedit:<file>|<old>|<new>  replace text in <project>/Editor/<file> (stands in for the code editor in tests; the arguments may hold spaces)
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include <imgui.h>

#include "app.h"
#include "ext_host.h"
#include "panel_registry.h"
#include "panels/asset_ui.h"
#include "panels/drawers/drawers.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/extension.h"
#include "rensga/editor/script_build.h"
#include "theme.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct ExtState {
  std::string root;                       // project the state belongs to
  std::string builtHash, pendingHash, seenHash;
  double seenSince = 0, pollStamp = 0;
  ScriptBuildJob job;
  bool failed = false, showStatus = false;
  int buildNumber = 0, errors = 0, warnings = 0;
  std::string lastResult;
  App* app = nullptr;                     // the application while a user callback runs (DrawDefaultInspector)
  std::set<std::string> reported;         // exceptions already logged
};
ExtState& st() {
  static ExtState s;
  return s;
}

std::string underscored(std::string s) {
  std::replace(s.begin(), s.end(), ' ', '_');
  return s;
}

// A user callback must not take the editor down with an exception; the message names where it came from.
template <class Fn>
void guarded(const char* what, const std::string& name, Fn&& fn) {
  try {
    fn();
  } catch (const std::exception& e) {
    const std::string key = std::string(what) + name + e.what();
    if (st().reported.insert(key).second) EditorLog::instance().error(std::string("extension ") + what + " '" + name + "' threw: " + e.what());
  } catch (...) {
    const std::string key = std::string(what) + name + "?";
    if (st().reported.insert(key).second) EditorLog::instance().error(std::string("extension ") + what + " '" + name + "' threw an unknown exception");
  }
}

// ---- building ------------------------------------------------------------------------------------------------------------

void startBuild(App& a, const std::string& hash) {
  ExtState& s = st();
  const ext::ExtensionLayout lay = ext::extensionLayout(a.project.root);
  ScriptBuildJob::Options o;
  o.commands = lay.commands;
  o.cwd = lay.buildDir.parent_path();
  { std::error_code ec; fs::create_directories(o.cwd, ec); fs::create_directories(lay.output.parent_path(), ec); }
  s.pendingHash = hash;
  s.failed = false;
  ++s.buildNumber;
  s.errors = s.warnings = 0;
  EditorLog::instance().info("editor extensions changed: compiling Editor/ in the background");
  if (!s.job.start(o)) EditorLog::instance().error("extensions: a build is already running");
}

void pumpLines(App& a) {
  ExtState& s = st();
  EditorLog& log = EditorLog::instance();
  for (const BuildLine& l : s.job.takeLines()) {
    if (l.isDiagnostic) {
      const Diagnostic& d = l.diag;
      fs::path f = d.file;
      std::error_code ec;
      if (!f.empty() && f.is_relative() && fs::exists(a.project.root / f, ec)) f = a.project.root / f;
      const LogLevel lvl = d.severity == DiagSeverity::Error ? LogLevel::Error : d.severity == DiagSeverity::Warning ? LogLevel::Warn : LogLevel::Info;
      log.addDiagnostic(lvl, formatDiagnostic(d, a.project.root), f.string(), d.line, d.col, 1000 + s.buildNumber);
      if (d.severity == DiagSeverity::Error) ++s.errors;
      else if (d.severity == DiagSeverity::Warning) ++s.warnings;
      continue;
    }
    float frac = 0;
    if (parseProgressLine(l.text, frac) && l.text.find("Linking") == std::string::npos && l.text.find("Built target") == std::string::npos) continue;
    if (l.text.rfind("make", 0) == 0 || l.text.rfind("gmake", 0) == 0 || l.text.rfind("ninja: build stopped", 0) == 0) continue;
    log.info(l.text);
  }
}

void loadOrReload(App& a) {
  ExtState& s = st();
  ext::ExtensionModule& m = ext::ExtensionModule::instance();
  std::string err;
  const fs::path lib = ext::extensionLayout(a.project.root).output;
  const bool ok = m.loaded() ? m.reload(lib, err) : m.load(lib, err);
  if (ok) {
    s.failed = false;
    s.lastResult = "Editor extensions reloaded (generation " + std::to_string(m.generation()) + ")";
    EditorLog::instance().info("extensions: " + std::to_string(m.inspectorCount()) + " inspector(s), " + std::to_string(m.windowCount()) + " window(s), " +
                               std::to_string(m.menuCount()) + " menu item(s) (generation " + std::to_string(m.generation()) + ")");
  } else {
    s.failed = true;
    s.lastResult = "Editor extensions did not load";
    EditorLog::instance().error("extensions: cannot load the module: " + err);
  }
}

void finishBuild(App& a) {
  ExtState& s = st();
  EditorLog& log = EditorLog::instance();
  s.builtHash = s.pendingHash;
  switch (s.job.state()) {
    case ScriptBuildJob::State::Succeeded: loadOrReload(a); break;
    case ScriptBuildJob::State::Failed:
      s.failed = true;
      s.lastResult = "Editor extensions build failed: " + std::to_string(s.errors) + " error(s)";
      log.error("extensions: build failed (" + std::to_string(s.errors) + " error(s), " + std::to_string(s.warnings) + " warning(s)); the previous module stays loaded");
      break;
    case ScriptBuildJob::State::Cancelled: s.failed = false; log.warn("extensions: build cancelled"); break;
    default: break;
  }
}

// Project opened or closed: the module of another project must go.
void switchProject(App& a) {
  ExtState& s = st();
  ext::ExtensionModule& m = ext::ExtensionModule::instance();
  if (s.job.busy()) s.job.cancel();
  if (m.loaded()) m.unload();
  s.builtHash.clear();
  s.seenHash.clear();
  s.failed = false;
  s.lastResult.clear();
  s.root = a.loaded ? a.project.root.string() : std::string();
  if (!a.loaded || !ext::projectHasEditorCode(a.project.root)) return;
  const std::string hash = ext::hashEditorSources(a.project.root);
  if (ext::extensionModuleUpToDate(a.project.root)) {
    s.builtHash = hash;
    loadOrReload(a);
    return;
  }
  if (!a.scripts.enabled) {  // headless runs have no background job: build here (--screenshot, --ui-event, --no-auto-rebuild)
    EditorLog::instance().info("extensions: building the Editor/ module");
    std::string out;
    s.builtHash = hash;
    if (ext::buildExtensionModuleBlocking(a.project.root, out)) loadOrReload(a);
    else {
      s.failed = true;
      EditorLog::instance().error("extensions: the Editor/ module did not build");
      size_t at = 0;
      while (at < out.size()) {
        size_t nl = out.find('\n', at);
        if (nl == std::string::npos) nl = out.size();
        const std::string line = out.substr(at, nl - at);
        if (line.find("error") != std::string::npos) EditorLog::instance().error(line);
        at = nl + 1;
      }
    }
    return;
  }
  startBuild(a, hash);
}

// ---- shortcuts -------------------------------------------------------------------------------------------------------------

ImGuiKeyChord parseChord(const std::string& text) {
  ImGuiKeyChord chord = 0;
  size_t at = 0;
  while (at <= text.size()) {
    size_t plus = text.find('+', at);
    if (plus == std::string::npos) plus = text.size();
    std::string part = text.substr(at, plus - at);
    at = plus + 1;
    for (char& c : part) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (part == "CTRL" || part == "CONTROL") chord |= ImGuiMod_Ctrl;
    else if (part == "SHIFT") chord |= ImGuiMod_Shift;
    else if (part == "ALT") chord |= ImGuiMod_Alt;
    else if (part.size() == 1 && part[0] >= 'A' && part[0] <= 'Z') chord |= ImGuiKey_A + (part[0] - 'A');
    else if (part.size() == 1 && part[0] >= '0' && part[0] <= '9') chord |= ImGuiKey_0 + (part[0] - '0');
    else if (part.size() >= 2 && part[0] == 'F' && std::atoi(part.c_str() + 1) >= 1 && std::atoi(part.c_str() + 1) <= 12) chord |= ImGuiKey_F1 + (std::atoi(part.c_str() + 1) - 1);
    else return 0;  // unknown key: no shortcut
  }
  return chord;
}

void runMenu(const ext::MenuEntry& e) { guarded("menu item", e.path, [&] { e.fn(); }); }

void handleShortcuts() {
  if (ImGui::GetIO().WantTextInput) return;
  const std::vector<ext::MenuEntry> items = ext::menuItems();
  for (const ext::MenuEntry& e : items) {
    if (e.shortcut.empty()) continue;
    const ImGuiKeyChord c = parseChord(e.shortcut);
    if (c && ImGui::IsKeyChordPressed(c)) runMenu(e);
  }
}

// ---- per frame ---------------------------------------------------------------------------------------------------------------

void tick(App& a) {
  ExtState& s = st();
  s.app = &a;
  ext::Host& h = ext::host();
  h.doc = a.loaded ? &a.doc : nullptr;
  h.assets = a.loaded ? &assetUi().sync.db() : nullptr;
  h.projectRoot = a.loaded ? a.project.root : fs::path();
  h.setStatus = [&a](const std::string& t) { a.status = t; };
  const std::string rootNow = a.loaded ? a.project.root.string() : std::string();
  if (rootNow != s.root) switchProject(a);
  if (!a.loaded) return;
  pumpLines(a);
  if (s.job.takeFinished()) {
    pumpLines(a);
    finishBuild(a);
  }
  if (!a.scripts.enabled || s.job.busy()) return;
  const double now = ImGui::GetTime();
  if (now - s.pollStamp < 0.25) return;
  s.pollStamp = now;
  const std::string hash = ext::hashEditorSources(a.project.root);
  if (hash.empty()) {  // no Editor/ sources any more: drop the module
    if (ext::ExtensionModule::instance().loaded()) ext::ExtensionModule::instance().unload();
    s.builtHash.clear();
    s.seenHash.clear();
    return;
  }
  if (hash == s.builtHash) { s.seenHash.clear(); return; }
  if (hash != s.seenHash) { s.seenHash = hash; s.seenSince = now; return; }
  if (now - s.seenSince >= 0.5) { s.seenHash.clear(); startBuild(a, hash); }
}

void drawStatus(App& a) {
  ExtState& s = st();
  if (!s.showStatus) return;
  ext::ExtensionModule& m = ext::ExtensionModule::instance();
  ImGui::SetNextWindowSize(ImVec2(440, 300), ImGuiCond_Appearing);
  if (!ImGui::Begin("Editor Extensions", &s.showStatus)) { ImGui::End(); return; }
  const bool hasCode = a.loaded && ext::projectHasEditorCode(a.project.root);
  if (!hasCode) {
    ImGui::TextWrapped("This project has no Editor/ folder. Put .cpp files with RENSGA_CUSTOM_INSPECTOR, RENSGA_EDITOR_WINDOW or RENSGA_MENU_ITEM in <project>/Editor/ and they load here.");
  } else {
    if (s.job.busy()) ImGui::TextColored(brandToken("info"), "[...] Building Editor/ (%d%%)", s.job.percent());
    else if (s.failed) ImGui::TextColored(brandToken("danger"), "[x] %s", s.lastResult.empty() ? "Editor/ build failed" : s.lastResult.c_str());
    else if (m.loaded()) ImGui::TextColored(brandToken("ok"), "[ok] Loaded, generation %d", m.generation());
    else ImGui::TextDisabled("Not loaded");
    ImGui::Text("%d custom inspector(s), %d window(s), %d menu item(s)", m.inspectorCount(), m.windowCount(), m.menuCount());
    ImGui::SeparatorText("Registered");
    for (const ext::InspectorEntry& e : ext::inspectors()) ImGui::BulletText("Inspector: %s%s", e.type.c_str(), e.replaceDefault ? " (replaces the fields)" : "");
    for (const ext::WindowEntry& e : ext::windows()) ImGui::BulletText("Window: %s", e.title.c_str());
    for (const ext::MenuEntry& e : ext::menuItems()) ImGui::BulletText("Menu: %s%s%s", e.path.c_str(), e.shortcut.empty() ? "" : "   ", e.shortcut.c_str());
    ImGui::BeginDisabled(s.job.busy());
    if (ImGui::Button("Rebuild now")) startBuild(a, ext::hashEditorSources(a.project.root));
    ImGui::EndDisabled();
  }
  ImGui::End();
}

void drawExtensions(App& a) {
  tick(a);
  if (!a.loaded) return;
  handleShortcuts();
  const std::vector<ext::WindowEntry> wins = ext::windows();
  for (const ext::WindowEntry& w : wins) {
    bool& open = ext::windowOpen(w.title);
    if (!open) continue;
    ImGui::SetNextWindowSize(ImVec2(360, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(w.title.c_str(), &open)) guarded("window", w.title, [&] { w.fn(); });
    ImGui::End();
  }
  drawStatus(a);
}

bool extHook(App& a, const std::string& act, const std::string& arg) {
  if (act.rfind("ext", 0) != 0) return false;
  ExtState& s = st();
  ext::ExtensionModule& m = ext::ExtensionModule::instance();
  if (act == "extbuild") {
    std::string out;
    s.builtHash = ext::hashEditorSources(a.project.root);
    if (ext::buildExtensionModuleBlocking(a.project.root, out)) { loadOrReload(a); std::printf("ext: build ok\n"); }
    else { s.failed = true; std::printf("ext: build failed\n%s\n", out.c_str()); }
  } else if (act == "extwait") {
    // handled by extHolds
  } else if (act == "extdump") {
    std::printf("ext: module loaded=%d generation=%d inspectors=%d windows=%d menus=%d building=%d failed=%d\n", m.loaded() ? 1 : 0, m.generation(),
                m.inspectorCount(), m.windowCount(), m.menuCount(), s.job.busy() ? 1 : 0, s.failed ? 1 : 0);
  } else if (act == "extwindow") {
    const size_t bar = arg.find('|');
    const std::string name = bar == std::string::npos ? arg : arg.substr(0, bar);
    const bool on = bar == std::string::npos || arg.substr(bar + 1) != "off";
    bool found = false;
    for (const ext::WindowEntry& w : ext::windows())
      if (underscored(w.title) == name) { ext::windowOpen(w.title) = on; found = true; }
    std::printf("ext: window %s %s\n", name.c_str(), found ? (on ? "open" : "closed") : "not found");
  } else if (act == "extmenu") {
    bool found = false;
    for (const ext::MenuEntry& e : ext::menuItems())
      if (underscored(e.path) == arg) { runMenu(e); found = true; break; }
    std::printf("ext: menu %s %s\n", arg.c_str(), found ? "ran" : "not found");
  } else if (act == "extedit") {
    const size_t b1 = arg.find('|');
    const size_t b2 = b1 == std::string::npos ? b1 : arg.find('|', b1 + 1);
    bool ok = false;
    if (b2 != std::string::npos) {
      const fs::path f = a.project.root / "Editor" / arg.substr(0, b1);
      std::ifstream in(f, std::ios::binary);
      std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      in.close();
      const std::string from = arg.substr(b1 + 1, b2 - b1 - 1), to = arg.substr(b2 + 1);
      const size_t at = text.find(from);
      if (at != std::string::npos && !from.empty()) {
        text.replace(at, from.size(), to);
        std::ofstream(f, std::ios::binary) << text;
        ok = true;
      }
    }
    std::printf("ext: edit %s\n", ok ? "ok" : "failed");
  } else if (act == "extstatus") {
    s.showStatus = arg != "off";
  } else {
    return false;
  }
  return true;
}

bool extHolds(App& a, const std::string& event) {
  if (event.rfind("extwait", 0) != 0) return false;
  ExtState& s = st();
  if (s.job.busy()) return true;
  if (!a.loaded || !a.scripts.enabled) return false;
  const std::string h = ext::hashEditorSources(a.project.root);
  return !h.empty() && h != s.builtHash;
}

}  // namespace

// ---- called by other panels -------------------------------------------------------------------------------------------------

int runExtensionInspectors(App& a, const std::string& type, const std::vector<ComponentRef>& refs, bool multi) {
  (void)multi;
  const std::vector<ext::InspectorEntry> list = ext::inspectors();
  int ran = 0;
  for (const ext::InspectorEntry& e : list) {
    if (e.type != type) continue;
    if (!inspectorGesture()) { a.doc.beginGesture(); inspectorGesture() = true; }  // widget drags in user code stay one undo step
    ext::ComponentView view(a.doc, refs, type);
    st().app = &a;
    guarded("inspector", type, [&] { e.fn(view); });
    ++ran;
  }
  return ran;
}

bool extensionReplacesFields(const std::string& type) { return ext::inspectorReplaces(type); }

void drawExtensionMenus(App& a) {
  (void)a;
  struct Node {
    std::string name;
    const ext::MenuEntry* leaf = nullptr;
    std::vector<Node> kids;
  };
  const std::vector<ext::MenuEntry> items = ext::menuItems();
  std::vector<Node> top;
  for (const ext::MenuEntry& e : items) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (at <= e.path.size()) {
      size_t slash = e.path.find('/', at);
      if (slash == std::string::npos) slash = e.path.size();
      if (slash > at) parts.push_back(e.path.substr(at, slash - at));
      at = slash + 1;
    }
    if (parts.size() < 2) continue;  // needs a menu and an item
    std::vector<Node>* level = &top;
    for (size_t i = 0; i < parts.size(); ++i) {
      auto it = std::find_if(level->begin(), level->end(), [&](const Node& n) { return n.name == parts[i] && (i + 1 < parts.size() ? !n.leaf : false); });
      if (it == level->end()) {
        level->push_back(Node{parts[i], nullptr, {}});
        it = level->end() - 1;
      }
      if (i + 1 == parts.size()) it->leaf = &e;
      else level = &it->kids;
    }
  }
  struct Drawer {
    static void run(const std::vector<Node>& nodes) {
      for (const Node& n : nodes) {
        if (n.leaf) {
          if (ImGui::MenuItem(n.name.c_str(), n.leaf->shortcut.empty() ? nullptr : n.leaf->shortcut.c_str())) runMenu(*n.leaf);
        } else if (ImGui::BeginMenu(n.name.c_str())) {
          run(n.kids);
          ImGui::EndMenu();
        }
      }
    }
  };
  Drawer::run(top);
}

void drawExtensionWindowItems(App& a) {
  ImGui::Separator();
  if (ImGui::MenuItem("Editor Extensions", nullptr, st().showStatus, a.loaded)) st().showStatus = !st().showStatus;
  for (const ext::WindowEntry& w : ext::windows()) ImGui::MenuItem(w.title.c_str(), nullptr, &ext::windowOpen(w.title));
}

RENSGA_EDITOR_PANEL(editor_extensions, "Editor Extensions", drawExtensions, extHook, extHolds, 900, 540, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor

namespace editor {
namespace ext {

// Unity's DrawDefaultInspector: the generated fields of the inspected component, each through its drawer, edits undoable.
void DrawDefaultInspector(ComponentView& view) {
  App* app = st().app;
  const TypeInfo* ti = view.TypeOf();
  if (!app || !ti) return;
  registerBuiltinDrawers();
  if (!ImGui::BeginTable("##ext_default", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) return;
  ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110);
  ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
  for (u32 i = 0; i < ti->fieldCount; ++i) {
    const FieldDesc& f = ti->fields[i];
    if (f.hideInInspector) continue;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(prettyFieldName(f.name).c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(f.name);
    DrawCtx ctx{*app, f, view.ObjectGuid() + "/ext/" + f.name, view.ObjectGuid(), view.Multi() && view.Mixed(f.name)};
    json nv;
    FieldDrawer draw = drawerFor(ti->name, f);
    if (draw && draw(ctx, view.Get(f.name), nv)) view.Set(f.name, nv);
    ImGui::PopID();
  }
  ImGui::EndTable();
}

}  // namespace ext
}  // namespace editor
}  // namespace rensga
