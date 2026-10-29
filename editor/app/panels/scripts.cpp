// Scripts in the editor (MVP-54..57): assign, create, and the rebuild + restart that stands in for Unity's domain
// reload. MVP deviation: project scripts are compiled into the editor, CLI, cooker and headless runtime at build time,
// so a changed script means rebuilding them and re-executing the editor with its state restored (hot reload: Editor 4).
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <set>

#include "app.h"
#include "rensga/core/reflect_json.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

json defaultFieldsFor(const TypeInfo& ti) {
  static std::map<std::string, json> cache;
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

bool scriptCompiled(const std::string& className) {
  const TypeInfo* ti = TypeRegistry::instance().find(className.c_str());
  return ti && ti->asBehaviour;
}

std::string scriptStatus(const App& a, const std::string& className) {
  if (scriptCompiled(className)) return "";
  return a.scripts.builder.busy() ? "compiling..." : "needs rebuild";
}

bool addScriptToObject(App& a, const std::string& objectGuid, const std::string& className) {
  EditorLog& log = EditorLog::instance();
  if (!a.doc.find(objectGuid)) return false;
  const TypeInfo* ti = TypeRegistry::instance().find(className.c_str());
  if (!ti || !ti->asBehaviour) {
    log.warn("script " + className + " is not compiled into this editor yet (" + scriptStatus(a, className) + ")");
    return false;
  }
  allowComponentInProject(a.project.root, className);  // the cook only accepts components the ConsoleTarget lists
  if (!a.doc.addComponent(objectGuid, className, defaultFieldsFor(*ti))) return false;
  a.doc.select(objectGuid);
  log.info("added " + className + " to " + a.doc.find(objectGuid)->name);
  return true;
}

bool createScriptAction(App& a, const std::string& name) {
  EditorLog& log = EditorLog::instance();
  if (!a.loaded) { log.error("create script: no project loaded"); return false; }
  std::set<std::string> known;
  const TypeRegistry& reg = TypeRegistry::instance();
  for (u32 i = 0; i < reg.count(); ++i)
    if (const TypeInfo* ti = reg.byId(i)) known.insert(ti->name);
  std::string why = validateScriptName(a.project.root, name, known);
  if (!why.empty()) { log.error("create script: " + why); a.scripts.newScriptError = why; return false; }
  fs::path file;
  if (!createScriptAsset(a.project.root, name, file, why)) { log.error("create script: " + why); return false; }
  log.info("created " + fs::relative(file, a.project.root).string() + " (rebuild runs when the editor regains focus or on Play)");
  a.scripts.scanStamp = -100;
  a.assetsStamp = -100;
  return true;
}

ScriptSync scriptsSync(App& a) {
  ScriptState& s = a.scripts;
  if (!s.enabled || !a.loaded) return ScriptSync::Clean;
  if (s.builder.busy()) return ScriptSync::Building;
  const std::string hash = hashProjectScripts(a.project.root);
  if (hash.empty()) return ScriptSync::Clean;
  std::error_code ec;
  const bool sameProject = fs::weakly_canonical(s.compiledProject, ec) == fs::weakly_canonical(a.project.root, ec);
  if (sameProject && readScriptsHash(a.project.root) == hash) { s.failed = false; return ScriptSync::Clean; }
  if (s.failed && s.failedHash == hash) return ScriptSync::Failed;  // retry only after the files change again
  ScriptBuildConfig cfg;
  cfg.buildDir = s.buildDir;
  cfg.project = a.project.root;
  cfg.cmake = s.cmake;
  cfg.targets = {"rensga-editor", "rensga", "rensga-cooker", "rensga_headless"};
  s.pendingHash = hash;
  s.failed = false;
  EditorLog::instance().info("scripts changed: rebuilding the editor, CLI, cooker and runtime");
  s.builder.start(cfg);
  return ScriptSync::Building;
}

void scriptsOnFocus(App& a) { scriptsSync(a); }

bool scriptsBlockPlay(const App& a) { return a.scripts.enabled && a.scripts.failed; }

void tickScripts(App& a) {
  ScriptState& s = a.scripts;
  if (a.loaded && ImGui::GetTime() - s.scanStamp > 2.0) {
    s.list = scanProjectScripts(a.project.root);
    s.scanStamp = ImGui::GetTime();
  }
  switch (s.builder.poll()) {
    case ScriptBuilder::Result::Ok:
      writeScriptsHash(a.project.root, s.pendingHash);
      EditorLog::instance().info("scripts: build succeeded, restarting the editor");
      s.restartRequested = true;
      break;
    case ScriptBuilder::Result::Failed:
      s.failed = true;
      s.failedHash = s.pendingHash;
      s.playAfter = false;
      EditorLog::instance().error("scripts: compile failed. Fix the errors above; the editor retries when it regains focus");
      break;
    default: break;
  }
}

// ---- editor state across the restart ----

static fs::path stateFile(const App& a) { return a.project.root / "Library" / "Editor" / "reload_state.json"; }

bool saveReloadState(App& a, const std::string& focusedWindow, bool playAfter) {
  if (!a.loaded) return false;
  a.sv.cam.save(a.cameraFile());
  nlohmann::ordered_json st = nlohmann::ordered_json::object();
  st["version"] = 1;
  st["project"] = a.project.root.string();
  if (a.doc.loaded()) {
    st["scenePath"] = a.doc.path().string();
    st["dirty"] = a.doc.dirty();
    st["scene"] = a.doc.document();
    st["selected"] = a.doc.selected();
  }
  st["focus"] = focusedWindow;
  st["playAfter"] = playAfter;
  std::error_code ec;
  fs::create_directories(stateFile(a).parent_path(), ec);
  std::ofstream o(stateFile(a), std::ios::binary | std::ios::trunc);
  o << st.dump(1) << "\n";
  return static_cast<bool>(o);
}

bool restoreReloadState(App& a) {
  EditorLog& log = EditorLog::instance();
  const fs::path f = stateFile(a);
  std::ifstream in(f, std::ios::binary);
  if (!in) { log.warn("restore: no saved editor state"); return false; }
  nlohmann::ordered_json st = nlohmann::ordered_json::parse(in, nullptr, false);
  in.close();
  std::error_code ec;
  fs::remove(f, ec);
  if (st.is_discarded() || !st.is_object()) { log.warn("restore: saved editor state is unreadable"); return false; }
  if (st.contains("scene") && st.contains("scenePath")) {
    if (a.doc.restore(st["scenePath"].get<std::string>(), st["scene"], st.value("dirty", false))) {
      a.doc.select(st.value("selected", std::string()));
      log.info(std::string("restored scene ") + a.doc.path().filename().string() + (a.doc.dirty() ? " (unsaved changes kept)" : ""));
    }
  }
  a.restoreFocus = st.value("focus", std::string());
  a.restoredPlayAfter = st.value("playAfter", false);
  return true;
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
  ImGui::BeginDisabled(!why.empty());
  if (ImGui::Button("Create", ImVec2(96, 0)) || (enter && why.empty())) {
    if (createScriptAction(a, s.newName)) ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(96, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

}  // namespace editor
}  // namespace rensga
