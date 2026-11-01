#include "emulator_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <fstream>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "app.h"
#include "panel_registry.h"
#include "modules.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/modules_model.h"
#include "rensga/pipeline/pipeline.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

namespace {

enum class EmuState { Unknown, Missing, Match, Differs, NoVersion };

struct EmulatorUi {
  bool show = false;
  // what the emulator is, refreshed on open, after an install and with the Refresh button (it starts a process for the version)
  bool infoValid = false;
  std::string target, exe, version, pin, moduleId;
  EmuState state = EmuState::Unknown;
  std::string notice;
  int noticeKind = 0;                  // 0 info, 1 ok, 2 warn, 3 error
  // the offer dialog
  bool openOffer = false;
  AfterEmulator after = AfterEmulator::None;
  bool installStarted = false;
  bool closeOffer = false;
  bool offerShown = false;             // the dialog was drawn this frame (for --ui-event emuinfo)
  // F6.05: Build and Run goes through App::play (PlayController), like Play on console; the separate Session of F5.09 is gone.
  bool runActive = false, runReadyLogged = false, runEndLogged = false;
  // F6.05: PCSX2 BIOS prompt
  bool openBios = false;
  std::function<void()> onBios;
  char biosInput[512] = {};
  std::string biosNote;
  // --ui-event buildrunwait
  std::chrono::steady_clock::time_point waitStart;
  bool waiting = false;
};

EmulatorUi& ui() {
  static EmulatorUi u;
  return u;
}

const char* stateWord(EmuState s) {
  switch (s) {
    case EmuState::Missing: return "missing";
    case EmuState::Match: return "match";
    case EmuState::Differs: return "differs";
    case EmuState::NoVersion: return "unknown-version";
    default: return "unknown";
  }
}

const ModuleRow* emulatorRow(App& a, const std::string& target) {
  for (const ModuleRow& r : modulesModel(a).rows())
    if (r.kind == "emulator" && r.target == target) return &r;
  return nullptr;
}

void say(EmulatorUi& u, int kind, const std::string& text) {
  u.notice = text;
  u.noticeKind = kind;
  EditorLog::instance().add(LogSource::Editor, kind >= 3 ? LogLevel::Error : kind == 2 ? LogLevel::Warn : LogLevel::Info, "emulators: " + text);
}

void refreshInfo(App& a) {
  EmulatorUi& u = ui();
  u.infoValid = true;
  u.target = a.target;
  u.exe.clear();
  u.version.clear();
  u.moduleId.clear();
  u.state = EmuState::Unknown;
  if (const ModuleRow* r = emulatorRow(a, a.target)) { u.pin = r->version; u.moduleId = r->id; }
  const TargetModule* t = a.targetModule();
  if (!t) return;
  const EmulatorInfo e = t->emulator();
  std::error_code ec;
  if (e.path.empty() || !fs::exists(e.path, ec)) { u.state = EmuState::Missing; return; }
  u.exe = e.path.string();
  u.version = e.version;
  if (u.version.empty()) u.state = EmuState::NoVersion;
  else u.state = u.pin.empty() || u.version == u.pin ? EmuState::Match : EmuState::Differs;
}

bool emulatorFound(const EmulatorUi& u) { return u.state != EmuState::Missing && u.state != EmuState::Unknown; }

// ---- look (same icons as the modules window; every color has a word) ---------------------------------------------------------
void noticeLine(const std::string& text, int kind) {
  if (text.empty()) return;
  static const char* tok[] = {"info", "ok", "warn", "danger"};
  static const char* word[] = {"Note: ", "OK: ", "Warning: ", "Problem: "};
  const int k = std::clamp(kind, 0, 3);
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken(tok[k]));
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted((std::string(word[k]) + text).c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
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

void label(const char* k) {
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("text-2"));
  ImGui::TextUnformatted(k);
  ImGui::PopStyleColor();
}

void jobBar(const JobSnapshot& j, float width) {
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, brandRamp("cerrado.500"));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, brandToken("border"));
  if (j.total) ImGui::ProgressBar(j.fraction(), ImVec2(width, 12), "");
  else ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), ImVec2(width, 12), "");
  ImGui::PopStyleColor(2);
}

std::string jobLine(const ModulesModel& m, const JobSnapshot& j) {
  const ModuleRow* r = m.find(j.moduleId);
  const std::string name = r ? r->name : j.moduleId;
  if (j.phase == "download") return "Downloading " + name + ": " + formatBytes(j.done) + (j.total ? " of " + formatBytes(j.total) : "");
  if (j.phase == "verify") return "Checking sha256 of " + name;
  if (j.phase == "extract") return "Unpacking " + name;
  if (j.phase == "check") return "Testing " + name;
  return "Preparing " + name;
}

// ---- install / locate ---------------------------------------------------------------------------------------------------------
bool startInstall(App& a, const ModuleRow& r) {
  EmulatorUi& u = ui();
  ModulesModel& m = modulesModel(a);
  std::string why;
  if (m.startInstall(r.id, why)) { u.installStarted = true; return true; }
  say(u, 3, why);
  return false;
}

// One install/locate row for a module: status word, the pinned version, and what the user can do about it.
void moduleActions(App& a, const ModuleRow& r, const char* idSuffix) {
  ModulesModel& m = modulesModel(a);
  const JobSnapshot j = m.job();
  const bool mine = j.moduleId == r.id && j.state == JobState::Running;
  if (mine) {
    ImGui::TextUnformatted(jobLine(m, j).c_str());
    jobBar(j, 360);
    if (ImGui::Button((std::string("Cancel install##") + idSuffix).c_str())) m.cancel();
    return;
  }
  if (r.status == ModuleStatus::NotInstalled) {
    for (const std::string& l : r.licenseIds) {
      bool ok = m.accepted(l);
      if (ImGui::Checkbox(("I accept the license: " + l + "##acc" + idSuffix + l).c_str(), &ok)) m.accept(l, ok);
    }
    ImGui::BeginDisabled(m.busy() || !m.unacceptedLicenses(r.id).empty());
    if (primaryBtn((("Install " + r.name + " (" + formatBytes(r.totalSize ? r.totalSize : r.size) + ")##inst") + idSuffix).c_str())) startInstall(a, r);
    ImGui::EndDisabled();
  } else if (r.status == ModuleStatus::Manual) {
    // The user installs it (package manager, portable build); the editor finds PPSSPPSDL and the like on its own, or $RENSGA_PPSSPP.
    noticeLine(r.note.empty() ? "Install " + r.name + " yourself; the editor finds it on its own." : r.note, 0);
  } else if (r.status == ModuleStatus::Unpublished || r.status == ModuleStatus::NoHost) {
    noticeLine(r.name + " is not available for this computer yet. " + r.note, 2);
  }
}

// ---- Build and Run ------------------------------------------------------------------------------------------------------------
// 0 idle, 1 building / launching, 2 running, 3 ended ok, 4 failed. Follows App::play while the run this window started is the one in it.
int runState(App& a) {
  const EmulatorUi& u = ui();
  if (!u.runActive) return 0;
  switch (a.play.status()) {
    case PlayStatus::Building:
    case PlayStatus::Launching: return 1;
    case PlayStatus::Running: return 2;
    case PlayStatus::Stopped: return a.play.finished() && !a.play.lastOk() ? 4 : 3;
    case PlayStatus::Idle: return 1;
  }
  return 0;
}

bool runBusy(App& a) {
  const int st = runState(a);
  return st == 1 || st == 2;
}

void startBuildRun(App& a) {
  EmulatorUi& u = ui();
  if (!a.loaded || a.target.empty()) { EditorLog::instance().error("build-run: no project with a console target is open"); return; }
  if (a.play.busy()) { EditorLog::instance().warn("build-run: already running (stop Play first)"); return; }
  if (!gateTargetModule(a, a.target, AfterModule::None)) return;                   // build support module missing: its dialog
  if (!gateTargetEmulator(a, a.target, AfterEmulator::BuildRun)) return;           // emulator missing: our dialog
  if (a.doc.dirty()) {
    EditorLog::instance().info("build-run: saving the modified scene before the build");
    if (!a.saveScene()) return;
  }
  pipeline::BuildRequest req;
  req.project = a.project.root;
  req.target = a.target;
  req.platform = a.target;     // the Build Settings flow: release package, then the emulator
  req.release = true;
  req.launch = true;
  if (const char* h = std::getenv("RENSGA_HANG_TIMEOUT")) req.hangTimeoutSeconds = std::atoi(h);
  if (const char* h = std::getenv("RENSGA_PLAY_HEADLESS")) req.headless = *h && *h != '0';
  EditorLog::instance().info("build-run: release build of " + a.target + " for " + a.project.root.string());
  u.runActive = a.play.play(req);
  u.runReadyLogged = u.runEndLogged = false;
}

}  // namespace

void doBuildAndRun(App& a) { startBuildRun(a); }

std::string pcsx2Bios() {
  try {
    std::ifstream f(fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".config" / "rensga" / "emulators.json");
    if (!f) return {};
    const nlohmann::json j = nlohmann::json::parse(f);
    const std::string p = j.contains("pcsx2") && j["pcsx2"].is_object() ? j["pcsx2"].value("bios", std::string()) : std::string();
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec) ? p : std::string();
  } catch (...) {
    return {};
  }
}

bool setPcsx2Bios(const std::string& path, std::string& err) {
  std::error_code ec;
  if (path.empty() || !fs::is_regular_file(path, ec)) { err = "that file does not exist"; return false; }
  if (fs::file_size(path, ec) < (1u << 20)) { err = "that file is smaller than 1 MiB, a PS2 BIOS dump is 4 MiB"; return false; }
  const fs::path cfg = fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".config" / "rensga" / "emulators.json";
  nlohmann::json j = nlohmann::json::object();
  try {
    std::ifstream f(cfg);
    if (f) j = nlohmann::json::parse(f);
  } catch (...) { j = nlohmann::json::object(); }
  if (!j.is_object()) j = nlohmann::json::object();
  if (!j.contains("pcsx2") || !j["pcsx2"].is_object()) j["pcsx2"] = nlohmann::json::object();
  j["pcsx2"]["bios"] = fs::absolute(path).string();   // referenced in place, never copied
  fs::create_directories(cfg.parent_path(), ec);
  std::ofstream o(cfg);
  if (!o) { err = "cannot write " + cfg.string(); return false; }
  o << j.dump(2) << "\n";
  return true;
}

bool gatePcsx2Bios(App&, std::function<void()> onReady) {
  if (!pcsx2Bios().empty()) return true;
  EmulatorUi& u = ui();
  u.openBios = true;
  u.onBios = std::move(onReady);
  u.biosNote.clear();
  return false;
}

void stopBuildRun(App& a) {
  if (ui().runActive) a.play.stop();
}

void openEmulatorManager(App& a) {
  EmulatorUi& u = ui();
  u.show = true;
  refreshInfo(a);
}

bool gateTargetEmulator(App& a, const std::string& target, AfterEmulator after) {
  EmulatorUi& u = ui();
  if (modulesUi().noPrompt) return true;
  if (!a.targetModule() || a.target != target) return true;
  refreshInfo(a);
  if (emulatorFound(u)) return true;
  if (!emulatorRow(a, target)) return true;       // nothing cataloged: let the pipeline say what is missing
  u.after = after;
  u.installStarted = false;
  u.openOffer = true;
  return false;
}

static void continueAfter(App& a, AfterEmulator what) {
  if (what == AfterEmulator::PlayConsole) { EditorLog::instance().info("emulators: emulator ready, continuing with Play on console"); doPlayConsole(a); }
  else if (what == AfterEmulator::BuildRun) { EditorLog::instance().info("emulators: emulator ready, continuing with Build and Run"); startBuildRun(a); }
}

// ---- the window ---------------------------------------------------------------------------------------------------------------
static void drawWindow(App& a) {
  EmulatorUi& u = ui();
  if (!u.show) return;
  if (!u.infoValid || u.target != a.target) refreshInfo(a);
  ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  if (!ImGui::Begin("Emulators", &u.show)) { ImGui::End(); return; }
  ModulesModel& m = modulesModel(a);
  const TargetModule* t = a.targetModule();
  if (!a.loaded || !t) {
    ImGui::TextWrapped("Open a project with a console target to see its emulator.");
    ImGui::End();
    return;
  }
  const ModuleRow* row = emulatorRow(a, a.target);

  ImGui::PushFont(a.fonts.uiBold, 16);
  ImGui::Text("Emulator for %s", std::string(t->displayName()).c_str());
  ImGui::PopFont();
  ImGui::Spacing();
  if (ImGui::BeginTable("##emu", 2, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 150);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    auto rowOf = [](const char* k) { ImGui::TableNextRow(); ImGui::TableNextColumn(); label(k); ImGui::TableNextColumn(); };
    rowOf("Module");
    ImGui::TextUnformatted(row ? row->name.c_str() : "none cataloged for this target");
    rowOf("Catalog status");
    ImGui::TextUnformatted(row ? moduleStatusWord(row->status) : "-");
    rowOf("Pinned version");
    ImGui::TextUnformatted(u.pin.empty() ? "-" : u.pin.c_str());
    rowOf("Found");
    if (u.state == EmuState::Missing) {
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
      ImGui::TextUnformatted("Not found");
      ImGui::PopStyleColor();
    } else {
      ImGui::PushTextWrapPos(0);
      ImGui::TextUnformatted(u.exe.c_str());
      ImGui::PopTextWrapPos();
    }
    rowOf("Version");
    if (u.state == EmuState::Match) {
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("ok"));
      ImGui::Text("%s  (OK: matches the pin)", u.version.c_str());
      ImGui::PopStyleColor();
    } else if (u.state == EmuState::Differs) {
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
      ImGui::Text("%s  (Warning: the pin is %s)", u.version.c_str(), u.pin.c_str());
      ImGui::PopStyleColor();
    } else if (u.state == EmuState::NoVersion) {
      ImGui::TextUnformatted("could not be read");
    } else {
      ImGui::TextUnformatted("-");
    }
    ImGui::EndTable();
  }
  if (u.state == EmuState::Differs)
    noticeLine("Play works with other versions, but Rensga is tested with " + u.pin + ". Install the pinned version below or point the editor at it with Locate.", 2);
  if (u.state == EmuState::Missing) noticeLine("Play on console and Build and Run need the emulator.", 3);
  ImGui::Spacing();
  if (row && (u.state == EmuState::Missing || u.state == EmuState::Differs)) moduleActions(a, *row, "win");
  ImGui::Spacing();
  if (ImGui::Button("Refresh")) { refreshInfo(a); say(u, 0, "checked again"); }
  ImGui::SameLine();
  ImGui::BeginDisabled(!t->emulator().resetSandbox || a.play.busy() || runBusy(a));
  if (ImGui::Button("Reset emulator save area")) {
    const EmulatorInfo e = t->emulator();
    if (e.resetSandbox && e.resetSandbox(a.project.root)) say(u, 1, "the project's emulator save area and settings were removed (Library/Emulators)");
    else say(u, 3, "could not remove the emulator save area");
  }
  ImGui::EndDisabled();
  ImGui::Spacing();
  noticeLine(u.notice, u.noticeKind);

  ImGui::Separator();
  ImGui::PushFont(a.fonts.uiBold, 16);
  ImGui::TextUnformatted("Build and Run");
  ImGui::PopFont();
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("text-2"));
  ImGui::TextWrapped("Builds the release package of the project (EBOOT.PBP with icon, PARAM.SFO, music and licenses), then runs that file in the emulator. Output: Library/Build/%s-release.", a.target.c_str());
  ImGui::PopStyleColor();
  ImGui::Spacing();
  const bool busy = runBusy(a);
  ImGui::BeginDisabled(busy || a.play.busy());
  if (primaryBtn("Build and Run", ImVec2(160, 32))) startBuildRun(a);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!busy);
  if (ImGui::Button("Stop", ImVec2(96, 32))) stopBuildRun(a);
  ImGui::EndDisabled();
  if (u.runActive) {
    ImGui::SameLine();
    ImGui::TextUnformatted(consoleStatusText(a).c_str());
  }
  if (const ModuleRow* tc = m.forTarget(a.target)) {
    ImGui::Spacing();
    label(("Build support: " + tc->name + " (" + moduleStatusWord(tc->status) + ")").c_str());
  }
  ImGui::End();
}

// ---- the offer ----------------------------------------------------------------------------------------------------------------
static void drawOffer(App& a) {
  EmulatorUi& u = ui();
  if (u.openOffer) { ImGui::OpenPopup("Emulator missing"); u.openOffer = false; }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(580, 0));
  if (!ImGui::BeginPopupModal("Emulator missing", nullptr, ImGuiWindowFlags_NoResize)) { u.offerShown = false; return; }
  u.offerShown = true;
  ModulesModel& m = modulesModel(a);
  const ModuleRow* r = emulatorRow(a, a.target);
  const TargetModule* t = a.targetModule();
  const JobSnapshot j = m.job();
  const bool mine = u.installStarted && r && j.moduleId == r->id;
  if (!r || !t) {
    ImGui::TextUnformatted("The emulator of this target is no longer in the catalog.");
  } else {
    ImGui::PushFont(a.fonts.uiBold, 16);
    if (r->status == ModuleStatus::NotInstalled || (mine && j.state == JobState::Running))
      ImGui::TextWrapped("%s is not installed (%s). Install now?", r->name.c_str(), formatBytes(r->totalSize ? r->totalSize : r->size).c_str());
    else if (r->status == ModuleStatus::Manual)
      ImGui::TextWrapped("%s is not found on this computer.", r->name.c_str());
    else
      ImGui::TextWrapped("%s cannot be installed here.", r->name.c_str());
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("text-2"));
    ImGui::TextWrapped("Play on console and Build and Run start the project in %s's emulator (a separate program). It is tested with version %s.",
                       std::string(t->displayName()).c_str(), r->version.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
    if (mine && j.state == JobState::Running) {
      ImGui::TextUnformatted(jobLine(m, j).c_str());
      jobBar(j, -1);
    } else if (mine && (j.state == JobState::Failed || j.state == JobState::Cancelled)) {
      noticeLine(j.message, j.state == JobState::Failed ? 3 : 2);
    } else {
      moduleActions(a, *r, "offer");
    }
  }
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button("Open Emulators window")) { openEmulatorManager(a); ImGui::CloseCurrentPopup(); }
  ImGui::SameLine();
  if (ImGui::Button(mine && j.state == JobState::Running ? "Cancel install" : "Cancel", ImVec2(130, 0))) {
    if (mine && j.state == JobState::Running) m.cancel();
    else { ImGui::CloseCurrentPopup(); EditorLog::instance().info("emulators: dialog cancelled, nothing was installed"); }
  }
  if (u.closeOffer) { u.closeOffer = false; ImGui::CloseCurrentPopup(); }
  ImGui::EndPopup();
}

static void drawBiosDialog(App&) {
  EmulatorUi& u = ui();
  if (u.openBios) { ImGui::OpenPopup("PCSX2 needs your BIOS"); u.openBios = false; }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("PCSX2 needs your BIOS", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::PushTextWrapPos(520.0f);
  ImGui::TextUnformatted("PCSX2 cannot boot a PS2 disc image without a PS2 BIOS. Rensga never ships one: dump the BIOS from your own console and point to the file. "
                         "It is used where it is (nothing is copied) and the path is kept in your user settings, outside the project.");
  ImGui::PopTextWrapPos();
  ImGui::SetNextItemWidth(520.0f);
  ImGui::InputTextWithHint("##bios", "/path/to/scph39001.bin", u.biosInput, sizeof u.biosInput);
  if (!u.biosNote.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger")); ImGui::TextUnformatted(("Problem: " + u.biosNote).c_str()); ImGui::PopStyleColor(); }
  if (primaryBtn("Use this BIOS")) {
    std::string err;
    if (setPcsx2Bios(u.biosInput, err)) {
      EditorLog::instance().info(std::string("emulators: PCSX2 BIOS set to ") + u.biosInput);
      ImGui::CloseCurrentPopup();
      auto go = std::move(u.onBios);
      u.onBios = nullptr;
      if (go) go();
    } else {
      u.biosNote = err;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) { u.onBios = nullptr; ImGui::CloseCurrentPopup(); }
  ImGui::EndPopup();
}

void drawEmulatorManager(App& a) {
  EmulatorUi& u = ui();
  if (u.runActive && !u.runReadyLogged && a.play.status() == PlayStatus::Running) {
    u.runReadyLogged = true;
    EditorLog::instance().info("build-run: ready in " + std::to_string(a.play.lastReadyMs()) + " ms (release EBOOT running)");
  }
  if (u.runActive && !u.runEndLogged && a.play.status() == PlayStatus::Stopped && a.play.finished()) {
    u.runEndLogged = true;
    EditorLog::instance().info("build-run: the emulator closed");
  }
  // an install of the emulator finished: look again, and continue what Play or Build and Run wanted
  ModulesModel& m = modulesModel(a);
  const JobSnapshot j = m.job();
  const ModuleRow* r = emulatorRow(a, a.target);
  if (u.installStarted && r && j.moduleId == r->id && (j.state == JobState::Done || j.state == JobState::Failed || j.state == JobState::Cancelled)) {
    if (j.state == JobState::Done) {
      u.installStarted = false;
      refreshInfo(a);
      say(u, emulatorFound(u) ? 1 : 2, emulatorFound(u) ? r->name + " installed (" + u.version + ")" : r->name + " installed, but its program was not found");
      if (emulatorFound(u)) {
        const AfterEmulator what = u.after;
        u.after = AfterEmulator::None;
        u.closeOffer = true;
        continueAfter(a, what);
      }
    }
  }
  drawWindow(a);
  drawOffer(a);
  drawBiosDialog(a);
}

// ---- --ui-event verbs ---------------------------------------------------------------------------------------------------------
bool emulatorUiHolds(App& a, const std::string& event) {
  EmulatorUi& u = ui();
  if (event.rfind("buildrunwait", 0) != 0) return false;
  if (!u.waiting) { u.waiting = true; u.waitStart = std::chrono::steady_clock::now(); }
  const size_t c = event.find(':');
  std::string what = c == std::string::npos ? "running" : event.substr(c + 1);
  int timeout = 120;
  if (const size_t bar = what.rfind('|'); bar != std::string::npos) { timeout = std::atoi(what.c_str() + bar + 1); what = what.substr(0, bar); }
  const bool timedOut = std::chrono::steady_clock::now() - u.waitStart > std::chrono::seconds(timeout);
  const int st = runState(a);
  bool done = st == 0 || timedOut || st == 4;
  if (what == "running") done = done || st == 2 || st == 3;
  else if (what == "finished") done = done || st == 3;
  if (done) {
    u.waiting = false;
    std::printf("build-run: wait %s %s state=%d ready_ms=%ld\n", what.c_str(), timedOut ? "timed-out" : "done", st, u.runActive ? a.play.lastReadyMs() : 0L);
  }
  (void)a;
  return !done;
}

bool emulatorHook(App& a, const std::string& act, const std::string& arg) {
  EmulatorUi& u = ui();
  if (act == "emulators") { openEmulatorManager(a); std::printf("emulators: window open\n"); return true; }
  if (act == "emuinfo") {
    refreshInfo(a);
    const ModuleRow* r = emulatorRow(a, a.target);
    std::printf("emulators: target=%s module=%s status=%s pin=%s exe=%s version=%s state=%s offer=%s\n", a.target.c_str(), u.moduleId.c_str(),
                r ? moduleStatusWord(r->status) : "none", u.pin.c_str(), u.exe.empty() ? "-" : u.exe.c_str(), u.version.empty() ? "-" : u.version.c_str(), stateWord(u.state),
                u.offerShown || u.openOffer ? "open" : "closed");
    return true;
  }
  if (act == "emuinstall") {
    const ModuleRow* r = emulatorRow(a, a.target);
    if (!r) { std::printf("emuinstall: no emulator module for %s\n", a.target.c_str()); return true; }
    std::string why;
    if (modulesModel(a).startInstall(r->id, why)) { u.installStarted = true; std::printf("emuinstall: %s started\n", r->id.c_str()); }
    else std::printf("emuinstall: %s refused: %s\n", r->id.c_str(), why.c_str());
    return true;
  }
  if (act == "emuaccept") { modulesModel(a).accept(arg, true); std::printf("emuaccept: %s\n", arg.c_str()); return true; }
  if (act == "emugate") {
    const bool go = gateTargetEmulator(a, arg.empty() ? a.target : arg, AfterEmulator::None);
    std::printf("emugate: %s %s\n", (arg.empty() ? a.target : arg).c_str(), go ? "proceed" : "dialog");
    return true;
  }
  if (act == "emugateplay") {   // the Play on console path: offer first, Play continues after the install
    const bool go = gateTargetEmulator(a, a.target, AfterEmulator::PlayConsole);
    std::printf("emugateplay: %s\n", go ? "proceed" : "dialog");
    return true;
  }
  if (act == "emudialoginstall") {
    const ModuleRow* r = emulatorRow(a, a.target);
    std::string why;
    if (r && modulesModel(a).startInstall(r->id, why)) { u.installStarted = true; std::printf("emudialoginstall: started\n"); }
    else std::printf("emudialoginstall: %s\n", why.empty() ? "no module" : why.c_str());
    return true;
  }
  if (act == "emudialogcancel") { u.installStarted = false; u.after = AfterEmulator::None; std::printf("emudialogcancel\n"); return true; }
  if (act == "emureset") {
    const TargetModule* t = a.targetModule();
    const EmulatorInfo e = t ? t->emulator() : EmulatorInfo{};
    const bool ok = e.resetSandbox && e.resetSandbox(a.project.root);
    std::printf("emureset: %s\n", ok ? "ok" : "failed");
    return true;
  }
  if (act == "biosstate") { std::printf("bios: pcsx2=%s\n", pcsx2Bios().empty() ? "missing" : pcsx2Bios().c_str()); return true; }
  if (act == "biosdialog") { u.openBios = true; u.onBios = nullptr; std::printf("bios: dialog open\n"); return true; }
  if (act == "biosset") { std::string err; const bool ok = setPcsx2Bios(arg, err); std::printf("bios: set %s %s\n", ok ? "ok" : "refused", err.c_str()); return true; }
  if (act == "buildrun") { startBuildRun(a); std::printf("build-run: started=%d\n", runBusy(a) ? 1 : 0); return true; }
  if (act == "buildrunstop") { stopBuildRun(a); std::printf("build-run: stop asked\n"); return true; }
  if (act == "buildrunstate") {
    std::printf("build-run: state=%d ready_ms=%ld\n", runState(a), u.runActive ? a.play.lastReadyMs() : 0L);
    return true;
  }
  return false;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(emulator_manager, "Emulators", drawEmulatorManager, emulatorHook, emulatorUiHolds, 40, 20, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor
}  // namespace rensga
