// Scripts in the editor (F3.03): assign, create, and the background build that feeds hot reload. Script changes are
// compiled by a ScriptBuildJob on a worker thread (the editor keeps drawing), then GameModule reloads the library
// between two frames. A failed build keeps the old module loaded, like Unity.
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <new>
#include <set>

#include "app.h"
#include "panel_registry.h"
#include "rensga/editor/inspector_model.h"
#include "rensga/core/reflect_json.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/editor_prefs.h"
#include "rensga/editor/game_module.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

static std::map<std::string, json>& defaultsCache() {
  static std::map<std::string, json> cache;
  return cache;
}

json defaultFieldsFor(const TypeInfo& ti) {
  auto& cache = defaultsCache();
  auto it = cache.find(ti.name);
  if (it != cache.end()) return it->second;
  json j = json::object();
  if (ti.construct) {
    void* m = ::operator new(ti.size, std::align_val_t(ti.align));
    ti.construct(m);
    j = fieldsToJson(ti, m);
    if (ti.destruct) ti.destruct(m);
    ::operator delete(m, std::align_val_t(ti.align));
  }
  cache[ti.name] = j;
  return j;
}

static double nowSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool scriptCompiled(const std::string& className) {
  const TypeInfo* ti = TypeRegistry::instance().find(className.c_str());
  return ti && (ti->asBehaviour || ti->asComponent);
}

std::string scriptStatus(const App& a, const std::string& className) {
  if (scriptCompiled(className)) return "";
  return a.scripts.builder.busy() ? "compiling..." : (scriptsHaveErrors() ? "compile errors" : "needs rebuild");
}

bool addScriptToObject(App& a, const std::string& objectGuid, const std::string& className) {
  EditorLog& log = EditorLog::instance();
  if (!a.doc.find(objectGuid)) return false;
  const TypeInfo* ti = TypeRegistry::instance().find(className.c_str());
  if (!ti || !(ti->asBehaviour || ti->asComponent)) {
    log.warn("script " + className + " is not compiled yet (" + scriptStatus(a, className) + ")");
    return false;
  }
  allowComponentInProject(a.project.root, className);  // the cook only accepts components the ConsoleTarget lists
  if (!addComponentTo(a.doc, objectGuid, className)) return false;  // required components first, Reset() applied (F3.07)
  if (!a.doc.isSelected(objectGuid)) a.doc.select(objectGuid);
  log.info("added " + className + " to " + a.doc.find(objectGuid)->name);
  return true;
}

bool createScriptAction(App& a, const std::string& name, ScriptKind kind) {
  EditorLog& log = EditorLog::instance();
  if (!a.loaded) { log.error("create script: no project loaded"); return false; }
  std::set<std::string> known;
  const TypeRegistry& reg = TypeRegistry::instance();
  for (u32 i = 0; i < reg.count(); ++i)
    if (const TypeInfo* ti = reg.byId(i)) known.insert(ti->name);
  std::string why = validateScriptName(a.project.root, name, known);
  if (!why.empty()) { log.error("create script: " + why); a.scripts.newScriptError = why; return false; }
  fs::path file;
  if (!createScriptAsset(a.project.root, name, file, why, kind)) { log.error("create script: " + why); return false; }
  log.info("created " + fs::relative(file, a.project.root).string() + "; building scripts in the background");
  a.scripts.scanStamp = -100;
  a.assetsStamp = -100;
  return true;
}

// ---- the build ----

static void startBuild(App& a, const std::string& hash) {
  ScriptState& s = a.scripts;
  const ScriptModuleLayout lay = scriptModuleLayout(a.project.root);
  ScriptBuildJob::Options o;
  o.commands = lay.commands;
  o.cwd = lay.buildDir.parent_path();
  { std::error_code ec; fs::create_directories(o.cwd, ec); }  // a fresh project has no Library yet
  s.pendingHash = hash;
  s.failed = false;
  ++s.buildNumber;
  s.lastErrors = s.lastWarnings = 0;
  s.statusText.clear();
  EditorLog::instance().info("scripts changed: compiling in the background");
  if (!s.builder.start(o)) EditorLog::instance().error("scripts: a build is already running");
}

ScriptSync scriptsSync(App& a) {
  ScriptState& s = a.scripts;
  if (!s.enabled || !a.loaded) return ScriptSync::Clean;
  if (s.builder.busy()) return ScriptSync::Building;
  const std::string hash = hashProjectScripts(a.project.root);
  if (hash.empty() || hash == s.builtHash) return s.failed ? ScriptSync::Failed : ScriptSync::Clean;
  startBuild(a, hash);  // asked for explicitly (Play, focus): no debounce
  return ScriptSync::Building;
}

void scriptsOnFocus(App& a) { scriptsSync(a); }

bool scriptsBlockPlay(const App&) { return scriptsHaveErrors(); }

void scriptsCancel(App& a) {
  if (a.scripts.builder.busy()) {
    a.scripts.builder.cancel();
    EditorLog::instance().warn("scripts: cancelling the build");
  }
}

static std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

static void pumpBuildLines(App& a) {
  ScriptState& s = a.scripts;
  EditorLog& log = EditorLog::instance();
  for (const BuildLine& l : s.builder.takeLines()) {
    if (l.isDiagnostic) {
      const Diagnostic& d = l.diag;
      fs::path f = d.file;
      std::error_code ec;
      if (!f.empty() && f.is_relative() && fs::exists(a.project.root / f, ec)) f = a.project.root / f;
      const LogLevel lvl = d.severity == DiagSeverity::Error ? LogLevel::Error
                         : d.severity == DiagSeverity::Warning ? LogLevel::Warn : LogLevel::Info;
      log.addDiagnostic(lvl, formatDiagnostic(d, a.project.root), f.string(), d.line, d.col, s.buildNumber);
      if (d.severity == DiagSeverity::Error) ++s.lastErrors;
      else if (d.severity == DiagSeverity::Warning) ++s.lastWarnings;
      continue;
    }
    float frac = 0;
    if (parseProgressLine(l.text, frac) && l.text.find("Linking") == std::string::npos &&
        l.text.find("Built target") == std::string::npos)
      continue;  // "[ 42%] Building ..." only drives the progress bar
    if (l.text.rfind("make", 0) == 0 || l.text.rfind("gmake", 0) == 0 || l.text.rfind("ninja: build stopped", 0) == 0) continue;  // make noise after an error
    log.info(l.text);
  }
}

static void finishBuild(App& a) {
  ScriptState& s = a.scripts;
  EditorLog& log = EditorLog::instance();
  s.builtHash = s.pendingHash;  // retry only after the files change again
  switch (s.builder.state()) {
    case ScriptBuildJob::State::Succeeded: {
      std::string err;
      const ScriptModuleLayout lay = scriptModuleLayout(a.project.root);
      if (GameModule::instance().reload(lay.output, err)) {
        defaultsCache().clear();
        setScriptsHaveErrors(false);
        s.failed = false;
        s.statusText = "Scripts reloaded (generation " + std::to_string(GameModule::instance().generation()) + ")";
        log.info("scripts reloaded (generation " + std::to_string(GameModule::instance().generation()) + ")");
        s.statusKind = 1;
      } else {
        setScriptsHaveErrors(true);
        s.failed = true;
        s.statusText = "Script module did not load";
        s.statusKind = 2;
        log.error("scripts: the new module did not load: " + err);
      }
      break;
    }
    case ScriptBuildJob::State::Failed:
      setScriptsHaveErrors(true);
      s.failed = true;
      s.playAfter = false;
      s.statusText = "Script build failed: " + plural(s.lastErrors, "error", "errors") + ", " + plural(s.lastWarnings, "warning", "warnings");
      s.statusKind = 2;
      log.error("scripts: build failed (" + plural(s.lastErrors, "error", "errors") + ", " + plural(s.lastWarnings, "warning", "warnings") +
                "); the previous module stays loaded");
      break;
    case ScriptBuildJob::State::Cancelled:
      s.failed = false;
      s.playAfter = false;
      s.statusText = "Script build cancelled";
      s.statusKind = 0;
      log.warn("scripts: build cancelled");
      break;
    default: break;
  }
  if (s.playAfter) {
    s.playAfter = false;
    if (!s.failed) doPlay(a);
  }
}

void tickScripts(App& a) {
  ScriptState& s = a.scripts;
  if (!a.loaded) return;
  if (!s.prefsReady && !a.hub.settings.home.empty()) {
    setPrefsDir(a.hub.settings.home / ".config" / "rensga");
    s.prefsReady = true;
  }
  const double now = nowSeconds();
  if (ImGui::GetTime() - s.scanStamp > 2.0) {
    s.list = scanProjectScripts(a.project.root);
    s.scanStamp = ImGui::GetTime();
  }
  pumpBuildLines(a);
  if (s.builder.takeFinished()) {
    pumpBuildLines(a);
    finishBuild(a);
  }
  if (!s.enabled || s.builder.busy()) return;
  // Watch the sources; a change must be stable for 500 ms before a build starts.
  if (now - s.pollStamp < 0.25) return;
  s.pollStamp = now;
  const std::string hash = hashProjectScripts(a.project.root);
  if (hash.empty() || hash == s.builtHash) { s.seenHash.clear(); return; }
  if (hash != s.seenHash) { s.seenHash = hash; s.seenSince = now; return; }
  if (now - s.seenSince >= 0.5) { s.seenHash.clear(); startBuild(a, hash); }
}

// Project opened: load the module if it is current, else build it.
void scriptsOnProjectOpen(App& a) {
  ScriptState& s = a.scripts;
  s.builtHash.clear();
  s.seenHash.clear();
  s.failed = false;
  setScriptsHaveErrors(false);
  GameModule& gm = GameModule::instance();
  if (a.loaded && gm.loaded()) gm.unload();  // another project's scripts
  if (!s.enabled || !a.loaded || !projectHasScripts(a.project.root)) return;
  const std::string hash = hashProjectScripts(a.project.root);
  if (scriptModuleUpToDate(a.project.root)) {
    s.builtHash = hash;
    std::string err;
    if (!gm.loaded() && !gm.load(scriptModuleLayout(a.project.root).output, err))
      EditorLog::instance().error("scripts: cannot load the module: " + err);
    return;
  }
  startBuild(a, hash);
}

// A diagnostic in the console: open its file at the line.
bool openLogLine(const LogLine& l) {
  if (l.file.empty()) return false;
  if (!openInExternalEditor(l.file, l.line, l.col)) {
    EditorLog::instance().warn("cannot open " + l.file + " with: " + externalEditorCommand());
    return false;
  }
  EditorLog::instance().info("opened " + l.file + ":" + std::to_string(l.line) + " in the code editor");
  return true;
}

bool openBuildError(int index) {
  int n = 0;
  for (const LogLine& l : EditorLog::instance().snapshot())
    if (l.level == LogLevel::Error && !l.file.empty() && n++ == index) return openLogLine(l);
  EditorLog::instance().warn("no build error number " + std::to_string(index));
  return false;
}

// ---- test tooling for --ui-event runs ----

// waitbuild: hold the event queue until no build runs and no source change is pending. waitbusy: until one runs.
bool uiEventHolds(App& a, const std::string& e) {
  const bool waitBuild = e.rfind("waitbuild", 0) == 0, waitBusy = e.rfind("waitbusy", 0) == 0;
  static std::string waitingFor;
  static double since = 0;
  if (!waitBuild && !waitBusy) { waitingFor.clear(); return false; }
  const double now = nowSeconds();
  if (waitingFor != e) { waitingFor = e; since = now; }
  const double limit = waitBusy ? 60 : 300;
  if (now - since > limit) {
    EditorLog::instance().error("ui-event " + e + " timed out");
    waitingFor.clear();
    return false;
  }
  bool hold;
  if (waitBusy) {
    hold = !a.scripts.builder.busy();
  } else {
    const std::string h = a.loaded ? hashProjectScripts(a.project.root) : std::string();
    hold = a.scripts.builder.busy() || (a.scripts.enabled && !h.empty() && h != a.scripts.builtHash);
  }
  if (!hold) waitingFor.clear();
  return hold;
}

// edit:<relative file>|<old text>|<new text>: replaces text in a project file, as a code editor save would.
bool scriptsTestEdit(App& a, const std::string& arg) {
  const size_t p1 = arg.find('|'), p2 = p1 == std::string::npos ? p1 : arg.find('|', p1 + 1);
  if (p2 == std::string::npos) { EditorLog::instance().error("edit: expected file|old|new"); return false; }
  const fs::path f = a.project.root / arg.substr(0, p1);
  std::ifstream in(f, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  const std::string from = arg.substr(p1 + 1, p2 - p1 - 1), to = arg.substr(p2 + 1);
  const size_t at = text.find(from);
  if (at == std::string::npos) { EditorLog::instance().error("edit: text not found in " + f.string()); return false; }
  text.replace(at, from.size(), to);
  std::ofstream o(f, std::ios::binary | std::ios::trunc);
  o << text;
  EditorLog::instance().info("edit: changed " + f.filename().string());
  return static_cast<bool>(o);
}

// ---- New C++ Script prompt ----

void drawScriptDialogs(App& a) {
  ScriptState& s = a.scripts;
  if (s.openNewScript) {
    ImGui::OpenPopup("New C++ Script");
    s.openNewScript = false;
    s.newScriptError.clear();
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("New C++ Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextUnformatted("Script name (also the class name)");
  const bool kindIsComponent = s.newKind == 1;
  ImGui::SetNextItemWidth(320);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  bool enter = ImGui::InputText("##scriptname", s.newName, sizeof s.newName,
                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  std::set<std::string> known;
  const TypeRegistry& reg = TypeRegistry::instance();
  for (u32 i = 0; i < reg.count(); ++i)
    if (const TypeInfo* ti = reg.byId(i)) known.insert(ti->name);
  const std::string why = validateScriptName(a.project.root, s.newName, known);
  if (!why.empty() && s.newName[0]) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
    ImGui::PushTextWrapPos(340);
    ImGui::TextUnformatted(why.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  } else {
    ImGui::TextDisabled("Creates Assets/Scripts/%s.cpp", s.newName[0] ? s.newName : "<Name>");
  }
  ImGui::Spacing();
  if (ImGui::RadioButton("Behaviour (Awake / Start / Update)", s.newKind == 0)) s.newKind = 0;
  if (ImGui::RadioButton("Empty Component (data only)", s.newKind == 1)) s.newKind = 1;
  ImGui::Spacing();
  ImGui::BeginDisabled(!why.empty());
  if (ImGui::Button("Create", ImVec2(96, 0)) || (enter && why.empty())) {
    if (createScriptAction(a, s.newName, kindIsComponent ? ScriptKind::Component : ScriptKind::Behaviour)) ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(96, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(script_waits, "Scripts (waits)", nullptr, nullptr, uiEventHolds, 0, 0, ::rensga::editor::PanelDraw::None)

}  // namespace editor
}  // namespace rensga
