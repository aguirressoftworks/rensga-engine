#include "emulator_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

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

struct BuildRun {
  pipeline::Session session;
  std::thread th;
  std::atomic<int> state{0};          // 0 idle, 1 building, 2 running, 3 ended ok, 4 failed
  std::atomic<long> readyMs{0};       // click to emulator running
  std::atomic<bool> stopAsked{false};
  std::mutex mu;
  std::vector<std::pair<int, std::string>> lines;   // level 0 info, 1 warn, 2 error, 3 game
  std::string summary;                 // last status sentence for the window
  std::chrono::steady_clock::time_point t0;
  ~BuildRun() {
    session.stop();
    if (th.joinable()) th.join();
  }
};

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
  std::unique_ptr<BuildRun> run;
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
void flushRun(BuildRun& r) {
  std::vector<std::pair<int, std::string>> take;
  {
    std::lock_guard<std::mutex> g(r.mu);
    take.swap(r.lines);
  }
  for (auto& [lvl, text] : take) {
    if (lvl == 3) EditorLog::instance().game(text);
    else EditorLog::instance().add(LogSource::Editor, lvl == 2 ? LogLevel::Error : lvl == 1 ? LogLevel::Warn : LogLevel::Info, text);
  }
}

void runWorker(BuildRun* r, pipeline::BuildRequest req) {
  auto push = [r](int lvl, std::string t) { std::lock_guard<std::mutex> g(r->mu); r->lines.emplace_back(lvl, std::move(t)); };
  const bool ok = r->session.run(req, [&](const pipeline::Event& e) {
    std::string t = std::string("build-run: ") + pipeline::stageName(e.stage) + ": " + e.message;
    if (e.status == pipeline::Status::Failed) push(2, t);
    else if (e.status == pipeline::Status::Progress) {   // tool output is long: keep the lines that say what happens
      static const char* const keep[] = {"warning", "package:", "package art", "compile skipped", "configure skipped", "configuring", "building",
                                         "toolchain", "reflection", "pipeline:", "cook:", "PPSSPP"};
      bool show = e.message.find("error") != std::string::npos || e.message.find("Error") != std::string::npos;
      for (const char* k : keep) show = show || e.message.rfind(k, 0) == 0;
      if (show) push(e.message.rfind("warning", 0) == 0 ? 1 : 0, t);
    } else {
      push(0, t);
    }
    if (e.stage == pipeline::Stage::Launch && e.status == pipeline::Status::Started) r->state = 1;
  });
  r->readyMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - r->t0).count();
  if (!ok) {
    r->state = 4;
    std::lock_guard<std::mutex> g(r->mu);
    r->summary = "failed (see the Console)";
    return;
  }
  r->state = 2;
  {
    std::lock_guard<std::mutex> g(r->mu);
    r->summary = "running, ready " + std::to_string(r->readyMs.load()) + " ms after the click";
    r->lines.emplace_back(0, "build-run: ready in " + std::to_string(r->readyMs.load()) + " ms (release EBOOT running)");
  }
  while (r->session.running()) {
    for (const std::string& l : r->session.drainGameLog()) push(3, l);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (const std::string& l : r->session.drainGameLog()) push(3, l);
  r->state = 3;
  std::lock_guard<std::mutex> g(r->mu);
  r->summary = "ended";
  r->lines.emplace_back(0, "build-run: the emulator closed");
}

bool runBusy(const EmulatorUi& u) { return u.run && (u.run->state == 1 || u.run->state == 2); }

void startBuildRun(App& a) {
  EmulatorUi& u = ui();
  if (!a.loaded || a.target.empty()) { EditorLog::instance().error("build-run: no project with a console target is open"); return; }
  if (runBusy(u)) { EditorLog::instance().warn("build-run: already running"); return; }
  if (a.play.busy()) { EditorLog::instance().warn("build-run: stop Play first"); return; }
  if (!gateTargetModule(a, a.target, AfterModule::None)) return;                   // build support module missing: its dialog
  if (!gateTargetEmulator(a, a.target, AfterEmulator::BuildRun)) return;           // emulator missing: our dialog
  if (a.doc.dirty()) {
    EditorLog::instance().info("build-run: saving the modified scene before the build");
    if (!a.saveScene()) return;
  }
  if (u.run) { stopBuildRun(a); if (u.run->th.joinable()) u.run->th.join(); }
  u.run = std::make_unique<BuildRun>();
  BuildRun* r = u.run.get();
  pipeline::BuildRequest req;
  req.project = a.project.root;
  req.target = a.target;
  req.release = true;
  req.launch = true;
  if (const char* h = std::getenv("RENSGA_HANG_TIMEOUT")) req.hangTimeoutSeconds = std::atoi(h);
  if (const char* h = std::getenv("RENSGA_PLAY_HEADLESS")) req.headless = *h && *h != '0';
  r->state = 1;
  r->t0 = std::chrono::steady_clock::now();
  r->summary = "building the release package";
  EditorLog::instance().info("build-run: release build of " + a.target + " for " + a.project.root.string());
  r->th = std::thread(runWorker, r, req);
}

}  // namespace

void doBuildAndRun(App& a) { startBuildRun(a); }

void stopBuildRun(App&) {
  EmulatorUi& u = ui();
  if (!u.run) return;
  u.run->stopAsked = true;
  u.run->session.stop();
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
  ImGui::BeginDisabled(!t->emulator().resetSandbox || a.play.busy() || runBusy(u));
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
  const bool busy = runBusy(u);
  ImGui::BeginDisabled(busy || a.play.busy());
  if (primaryBtn("Build and Run", ImVec2(160, 32))) startBuildRun(a);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!busy);
  if (ImGui::Button("Stop", ImVec2(96, 32))) stopBuildRun(a);
  ImGui::EndDisabled();
  if (u.run) {
    std::string s;
    { std::lock_guard<std::mutex> g(u.run->mu); s = u.run->summary; }
    ImGui::SameLine();
    ImGui::TextUnformatted(s.c_str());
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

void drawEmulatorManager(App& a) {
  EmulatorUi& u = ui();
  if (u.run) flushRun(*u.run);
  if (u.run && u.run->state >= 3 && u.run->th.joinable()) u.run->th.join();
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
  const int st = u.run ? u.run->state.load() : 0;
  bool done = st == 0 || timedOut || st == 4;
  if (what == "running") done = done || st == 2 || st == 3;
  else if (what == "finished") done = done || st == 3;
  if (done) {
    u.waiting = false;
    std::printf("build-run: wait %s %s state=%d ready_ms=%ld\n", what.c_str(), timedOut ? "timed-out" : "done", st, u.run ? u.run->readyMs.load() : 0L);
    if (u.run) flushRun(*u.run);
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
  if (act == "buildrun") { startBuildRun(a); std::printf("build-run: started=%d\n", runBusy(u) ? 1 : 0); return true; }
  if (act == "buildrunstop") { stopBuildRun(a); std::printf("build-run: stop asked\n"); return true; }
  if (act == "buildrunstate") {
    std::printf("build-run: state=%d ready_ms=%ld\n", u.run ? u.run->state.load() : 0, u.run ? u.run->readyMs.load() : 0L);
    return true;
  }
  return false;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(emulator_manager, "Emulators", drawEmulatorManager, emulatorHook, emulatorUiHolds, 40, 20, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor
}  // namespace rensga
