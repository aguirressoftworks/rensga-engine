// F6.05 (Editor 11): the Build Settings window. Scenes in the build (enable, reorder, add the open scene; written to
// ProjectSettings/Project.json "buildScenes" + "scenes"), the platforms of the project (its console, Windows, Linux, Android), the
// development flag and the output folder. Build validates every scene against the target's budgets, cooks, compiles and packages;
// Build and Run then starts the result through PlayController (PPSSPP, PCSX2, the player, wine, adb). Nothing here names a console:
// the platform list and what each one needs come from tools/pipeline (platform_build.h) and the TargetModules.
//
//   --ui-event verbs: buildsettings[:off] | bsplatform:<id> | bsdev:<0|1> | bsoutput:<folder> | bsscene:<add|remove|enable|disable|up|down>|<scene name>
//   | bsbuild | bsrun | bsstop | bswait:<finished|running>|<secs> | bsstate | bsscenes | bsplatforms
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "emulator_manager.h"
#include "modules.h"
#include "panel_registry.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/project_settings.h"
#include "rensga/pipeline/platform_build.h"
#include "rensga/pipeline/toolchain.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

namespace {

struct BuildSettingsUi {
  bool open = false;
  bool loaded = false;
  fs::path root;
  ProjectBuildSettings settings;
  std::string platform;               // selected platform id
  char outputBuf[256] = "Builds";
  std::vector<std::string> platforms; // ids offered for this project
  bool runAfter = false;
  bool active = false;                // the run in App::play was started by this window
  bool waiting = false;
  std::chrono::steady_clock::time_point waitStart;
  std::string note;
  int noteKind = 0;
  struct Status { std::string text; bool ready = false; };
  std::vector<std::pair<std::string, Status>> status;
  std::chrono::steady_clock::time_point statusAt;
};
BuildSettingsUi& ui() {
  static BuildSettingsUi u;
  return u;
}

void say(BuildSettingsUi& u, int kind, const std::string& text) {
  u.note = text;
  u.noteKind = kind;
  EditorLog::instance().add(LogSource::Editor, kind >= 3 ? LogLevel::Error : kind == 2 ? LogLevel::Warn : LogLevel::Info, "build settings: " + text);
}

bool primaryBtn(const char* label, ImVec2 size = ImVec2(0, 0)) {
  ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary"));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brandToken("primary-hover"));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, brandToken("primary-hover"));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
  const bool r = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return r;
}

std::string sceneLabel(App& a, const std::string& guid) {
  for (const Scene& s : a.project.scenes)
    if (s.guid == guid) return fs::path(s.path).stem().string().empty() ? s.path : fs::path(s.path).stem().string();
  return "Missing (" + guid + ")";
}
const Scene* findSceneByName(App& a, const std::string& name) {
  for (const Scene& s : a.project.scenes)
    if (fs::path(s.path).stem().string() == name || s.guid == name) return &s;
  return nullptr;
}

void load(App& a) {
  BuildSettingsUi& u = ui();
  u.root = a.project.root;
  std::string err;
  u.settings = ProjectBuildSettings::defaults();
  if (!loadProjectBuild(u.root, u.settings, err)) say(u, 3, "Project.json: " + err);
  u.platforms = pipeline::platformsForProject(u.root);
  const std::string saved = u.settings.extra.value("buildPlatform", std::string());
  if (u.platform.empty() || std::find(u.platforms.begin(), u.platforms.end(), u.platform) == u.platforms.end())
    u.platform = !saved.empty() && std::find(u.platforms.begin(), u.platforms.end(), saved) != u.platforms.end() ? saved : (u.platforms.empty() ? "" : u.platforms[0]);
  const std::string out = u.settings.extra.value("buildOutput", std::string("Builds"));
  std::snprintf(u.outputBuf, sizeof u.outputBuf, "%s", out.c_str());
  u.loaded = true;
  u.status.clear();
}

bool save(BuildSettingsUi& u) {
  u.settings.extra["buildPlatform"] = u.platform;
  u.settings.extra["buildOutput"] = std::string(u.outputBuf);
  std::string err;
  if (!saveProjectBuild(u.root, u.settings, err)) { say(u, 3, "cannot write Project.json: " + err); return false; }
  return true;
}

// What each platform needs here: the build support module, and a way to run the result.
BuildSettingsUi::Status probe(App&, const pipeline::PlatformInfo& pi) {
  BuildSettingsUi::Status st;
  std::string why;
  bool tool = true;
  if (!pi.moduleId.empty()) {
    pipeline::ToolchainRunner tr(pi.moduleId, pi.dockerWrapper.empty() ? fs::path() : fs::path(RENSGA_SOURCE_DIR) / "tools" / "docker" / (pi.dockerWrapper + ".sh"));
    std::string err;
    tool = tr.resolve(err);
    if (!tool) why = "build module " + pi.moduleId + " missing (rensga modules install " + pi.moduleId + ")";
  }
  std::string runWhy;
  if (pi.run.needs == "bios") {
    if (pcsx2Bios().empty()) runWhy = "the emulator needs your BIOS (asked on first run)";
  } else if (pi.run.kind == "wine") {
    if (std::system("command -v wine >/dev/null 2>&1") != 0) runWhy = "wine not found: builds, cannot run here";
  } else if (pi.run.kind == "adb") {
    runWhy = "runs only with an attached adb device";
  }
  st.ready = tool;
  st.text = tool ? (runWhy.empty() ? "ready" : "builds; " + runWhy) : why;
  return st;
}

void refreshStatus(App& a) {
  BuildSettingsUi& u = ui();
  u.status.clear();
  for (const std::string& id : u.platforms)
    if (const pipeline::PlatformInfo* pi = pipeline::findPlatform(id)) u.status.emplace_back(id, probe(a, *pi));
  u.statusAt = std::chrono::steady_clock::now();
}

const BuildSettingsUi::Status* statusOf(const std::string& id) {
  for (auto& s : ui().status)
    if (s.first == id) return &s.second;
  return nullptr;
}

int runState(App& a) {
  const BuildSettingsUi& u = ui();
  if (!u.active) return 0;
  switch (a.play.status()) {
    case PlayStatus::Building:
    case PlayStatus::Launching:
    case PlayStatus::Idle: return 1;
    case PlayStatus::Running: return 2;
    case PlayStatus::Stopped: return a.play.finished() && !a.play.lastOk() ? 4 : 3;
  }
  return 0;
}

void start(App& a, bool run) {
  BuildSettingsUi& u = ui();
  if (!a.loaded) { say(u, 3, "no project is open"); return; }
  if (!u.loaded || u.root != a.project.root) load(a);
  if (u.platform.empty()) { say(u, 3, "no platform is selected"); return; }
  if (a.play.busy() || a.session.active()) { say(u, 2, "stop Play first"); return; }
  if (u.settings.enabledScenes().empty()) { say(u, 3, "add at least one scene to the build"); return; }
  const pipeline::PlatformInfo* pi = pipeline::findPlatform(u.platform);
  if (!pi) return;
  if (run && pi->run.needs == "bios" && !gatePcsx2Bios(a, [&a] { start(a, true); })) { say(u, 2, "the emulator needs your BIOS first"); return; }
  if (run && pi->console && u.platform == a.target && !gateTargetEmulator(a, a.target, AfterEmulator::None)) return;
  if (a.doc.dirty()) {
    say(u, 0, "saving the modified scene before the build");
    if (!a.saveScene()) return;
  }
  save(u);
  pipeline::BuildRequest req;
  req.project = a.project.root;
  req.platform = u.platform;
  req.target = u.platform;
  req.development = u.settings.development;
  req.release = !u.settings.development;
  req.outputDir = u.outputBuf;
  req.lastStage = run ? pipeline::Stage::Launch : pipeline::Stage::Package;
  req.quitAfterSeconds = 0;
  if (const char* q = std::getenv("RENSGA_BS_QUIT_AFTER")) req.quitAfterSeconds = std::atoi(q);  // tests: the player ends by itself
  if (const char* h = std::getenv("RENSGA_PLAY_HEADLESS")) req.headless = *h && *h != '0';
  if (const char* h = std::getenv("RENSGA_HANG_TIMEOUT")) req.hangTimeoutSeconds = std::atoi(h);
  u.active = run ? a.play.play(req) : a.play.build(req);
  u.runAfter = run;
  say(u, 0, std::string(run ? "Build and Run " : "Build ") + pi->name + (req.development ? " (development)" : " (release)"));
}

// ---- window -------------------------------------------------------------------------------------------------------------------
void drawBuildSettings(App& a) {
  BuildSettingsUi& u = ui();
  if (!u.open) return;
  if (!a.loaded) return;
  if (!u.loaded || u.root != a.project.root) load(a);
  if (u.status.empty()) refreshStatus(a);
  ImGui::SetNextWindowSize(ImVec2(620, 640), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  if (!ImGui::Begin("Build Settings", &u.open)) { ImGui::End(); return; }

  ImGui::PushFont(a.fonts.uiBold, 16);
  ImGui::TextUnformatted("Scenes In Build");
  ImGui::PopFont();
  bool changed = false;
  int move = -1, moveTo = -1, removeAt = -1;
  for (size_t i = 0; i < u.settings.scenes.size(); ++i) {
    BuildScene& bs = u.settings.scenes[i];
    ImGui::PushID(static_cast<int>(i));
    bool en = bs.enabled;
    if (ImGui::Checkbox("##en", &en)) { bs.enabled = en; changed = true; }
    ImGui::SameLine();
    const std::string label = sceneLabel(a, bs.guid);
    const bool missing = label.rfind("Missing", 0) == 0;
    if (missing) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
    ImGui::Text("%zu  %s%s", i, missing ? "[x] " : "", label.c_str());
    if (missing) ImGui::PopStyleColor();
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 120.0f, ImGui::GetWindowContentRegionMax().x - 190.0f));
    if (ImGui::SmallButton("Up") && i > 0) { move = static_cast<int>(i); moveTo = static_cast<int>(i) - 1; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Down") && i + 1 < u.settings.scenes.size()) { move = static_cast<int>(i); moveTo = static_cast<int>(i) + 1; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) removeAt = static_cast<int>(i);
    ImGui::PopID();
  }
  if (u.settings.scenes.empty()) ImGui::TextDisabled("No scenes in the build. Add the open scene below.");
  if (move >= 0) { u.settings.moveScene(move, moveTo); changed = true; }
  if (removeAt >= 0) { u.settings.removeScene(u.settings.scenes[removeAt].guid); changed = true; }
  const std::string openGuid = a.doc.loaded() ? a.doc.scene().guid : std::string();
  bool inBuild = false;
  for (const BuildScene& bs : u.settings.scenes) inBuild = inBuild || bs.guid == openGuid;
  ImGui::BeginDisabled(openGuid.empty() || inBuild);
  if (ImGui::Button("Add Open Scene")) { u.settings.addScene(openGuid, true); changed = true; }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::BeginCombo("##addscene", "Add scene...")) {
    for (const Scene& s : a.project.scenes) {
      bool already = false;
      for (const BuildScene& bs : u.settings.scenes) already = already || bs.guid == s.guid;
      if (already) continue;
      if (ImGui::Selectable(fs::path(s.path).stem().string().c_str())) { u.settings.addScene(s.guid, true); changed = true; }
    }
    ImGui::EndCombo();
  }
  if (changed) save(u);

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::PushFont(a.fonts.uiBold, 16);
  ImGui::TextUnformatted("Platform");
  ImGui::PopFont();
  for (const std::string& id : u.platforms) {
    const pipeline::PlatformInfo* pi = pipeline::findPlatform(id);
    if (!pi) continue;
    const BuildSettingsUi::Status* st = statusOf(id);
    ImGui::PushID(id.c_str());
    if (ImGui::RadioButton(pi->name.c_str(), u.platform == id)) { u.platform = id; save(u); }
    ImGui::SameLine(150.0f);
    if (st) {
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken(st->ready ? "ok" : "warn"));
      ImGui::Text("%s %s", st->ready ? "[ok]" : "[!]", st->text.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::PopID();
  }
  if (ImGui::SmallButton("Refresh")) refreshStatus(a);

  ImGui::Spacing();
  bool dev = u.settings.development;
  if (ImGui::Checkbox("Development build", &dev)) { u.settings.development = dev; save(u); }
  ImGui::SameLine();
  ImGui::TextDisabled(dev ? "(profiler stream on, no release packaging)" : "(release: EBOOT.PBP for PSP, no profiler stream)");
  ImGui::SetNextItemWidth(280.0f);
  if (ImGui::InputText("Output folder", u.outputBuf, sizeof u.outputBuf, ImGuiInputTextFlags_EnterReturnsTrue)) save(u);
  if (ImGui::IsItemDeactivatedAfterEdit()) save(u);
  const pipeline::PlatformInfo* sel = pipeline::findPlatform(u.platform);
  if (sel) ImGui::TextDisabled("Output: %s/%s  (%s)", u.outputBuf, u.platform.c_str(), sel->output.c_str());

  ImGui::Spacing();
  ImGui::Separator();
  const int st = runState(a);
  const bool busy = st == 1 || st == 2 || a.play.busy();
  ImGui::BeginDisabled(busy || u.platform.empty());
  if (primaryBtn("Build and Run", ImVec2(150, 32))) start(a, true);
  ImGui::SameLine();
  if (ImGui::Button("Build", ImVec2(110, 32))) start(a, false);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!busy);
  if (ImGui::Button("Stop", ImVec2(90, 32))) a.play.stop();
  ImGui::EndDisabled();
  if (u.active) {
    ImGui::SameLine();
    const char* word = st == 1 ? "Building..." : st == 2 ? "Running" : st == 4 ? "Failed (see the Console)" : st == 3 ? (a.play.builtNotRun() ? "Built, not run" : "Done") : "";
    ImGui::TextUnformatted(word);
    if (st == 1) ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 6));
    if (st >= 3 && !a.play.lastOutput().empty()) ImGui::TextWrapped("Output: %s", a.play.lastOutput().string().c_str());
    if (a.play.builtNotRun()) ImGui::TextColored(brandToken("warn"), "[!] %s", a.play.lastNote().c_str());
  }
  if (!u.note.empty()) {
    static const char* tok[] = {"text-2", "ok", "warn", "danger"};
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken(tok[std::min(std::max(u.noteKind, 0), 3)]));
    ImGui::TextWrapped("%s", u.note.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::End();
}

bool bsHolds(App& a, const std::string& event) {
  BuildSettingsUi& u = ui();
  if (event.rfind("bswait", 0) != 0) return false;
  if (!u.waiting) { u.waiting = true; u.waitStart = std::chrono::steady_clock::now(); }
  const size_t c = event.find(':');
  std::string what = c == std::string::npos ? "finished" : event.substr(c + 1);
  int timeout = 900;
  if (const size_t bar = what.rfind('|'); bar != std::string::npos) { timeout = std::atoi(what.c_str() + bar + 1); what = what.substr(0, bar); }
  const bool timedOut = std::chrono::steady_clock::now() - u.waitStart > std::chrono::seconds(timeout);
  const int st = runState(a);
  bool done = st == 0 || st == 4 || timedOut;
  if (what == "running") done = done || st == 2 || st == 3;
  else done = done || st == 3;
  if (done) {
    u.waiting = false;
    std::printf("buildsettings: wait %s %s state=%d ok=%d\n", what.c_str(), timedOut ? "timed-out" : "done", st, st == 3 || st == 2 ? 1 : 0);
  }
  return !done;
}

bool bsHook(App& a, const std::string& act, const std::string& arg) {
  BuildSettingsUi& u = ui();
  if (act == "buildsettings") {
    u.open = arg != "off";
    if (u.open && a.loaded) { load(a); refreshStatus(a); }
    std::printf("buildsettings: window %s\n", u.open ? "open" : "closed");
    return true;
  }
  if (act.rfind("bs", 0) != 0) return false;
  if (!a.loaded) { std::printf("buildsettings: no project\n"); return true; }
  if (!u.loaded || u.root != a.project.root) load(a);
  if (act == "bsplatform") {
    const bool ok = std::find(u.platforms.begin(), u.platforms.end(), arg) != u.platforms.end();
    if (ok) { u.platform = arg; save(u); }
    std::printf("buildsettings: platform %s %s\n", arg.c_str(), ok ? "selected" : "not offered for this project");
    return true;
  }
  if (act == "bsdev") { u.settings.development = arg != "0"; save(u); std::printf("buildsettings: development=%d\n", u.settings.development ? 1 : 0); return true; }
  if (act == "bsoutput") { std::snprintf(u.outputBuf, sizeof u.outputBuf, "%s", arg.c_str()); save(u); std::printf("buildsettings: output=%s\n", u.outputBuf); return true; }
  if (act == "bsscene") {
    const size_t bar = arg.find('|');
    const std::string op = arg.substr(0, bar), name = bar == std::string::npos ? "" : arg.substr(bar + 1);
    const Scene* sc = findSceneByName(a, name);
    bool ok = sc != nullptr;
    if (ok) {
      int idx = -1;
      for (size_t i = 0; i < u.settings.scenes.size(); ++i)
        if (u.settings.scenes[i].guid == sc->guid) idx = static_cast<int>(i);
      if (op == "add") ok = u.settings.addScene(sc->guid, true);
      else if (op == "remove") ok = u.settings.removeScene(sc->guid);
      else if (op == "enable" || op == "disable") ok = u.settings.setSceneEnabled(sc->guid, op == "enable");
      else if (op == "up") ok = idx > 0 && u.settings.moveScene(idx, idx - 1);
      else if (op == "down") ok = idx >= 0 && u.settings.moveScene(idx, idx + 1);
      else ok = false;
      if (ok) save(u);
    }
    std::printf("buildsettings: scene %s %s %s\n", op.c_str(), name.c_str(), ok ? "ok" : "refused");
    return true;
  }
  if (act == "bsscenes") {
    std::printf("buildsettings: scenes");
    for (const BuildScene& bs : u.settings.scenes) std::printf(" %s%s", sceneLabel(a, bs.guid).c_str(), bs.enabled ? "" : "(off)");
    std::printf("\n");
    return true;
  }
  if (act == "bsplatforms") {
    refreshStatus(a);
    for (auto& s : u.status) std::printf("buildsettings: platform %s ready=%d %s\n", s.first.c_str(), s.second.ready ? 1 : 0, s.second.text.c_str());
    return true;
  }
  if (act == "bsbuild" || act == "bsrun") {
    start(a, act == "bsrun");
    std::printf("buildsettings: %s started=%d platform=%s dev=%d\n", act.c_str(), u.active ? 1 : 0, u.platform.c_str(), u.settings.development ? 1 : 0);
    return true;
  }
  if (act == "bsstop") { a.play.stop(); std::printf("buildsettings: stop asked\n"); return true; }
  if (act == "bsstate") {
    std::printf("buildsettings: state=%d platform=%s dev=%d output=%s notrun=%d note=%s\n", runState(a), u.platform.c_str(), u.settings.development ? 1 : 0,
                a.play.lastOutput().empty() ? "-" : a.play.lastOutput().string().c_str(), a.play.builtNotRun() ? 1 : 0, a.play.builtNotRun() ? a.play.lastNote().c_str() : "-");
    return true;
  }
  return false;
}

}  // namespace

// File > Build Settings... and Ctrl+Shift+B open this.
void openBuildSettings(App& a) {
  ui().open = true;
  if (a.loaded) { load(a); refreshStatus(a); }
}

RENSGA_EDITOR_PANEL(build_settings, "Build Settings", drawBuildSettings, bsHook, bsHolds, 880, 22, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor
}  // namespace rensga
