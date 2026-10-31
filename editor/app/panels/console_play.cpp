// F5.19-F5.22: --ui-event verbs for Play on console (the same code paths as the toolbar and the Console panel), used by
// tests/editor/emu_play_*.sh. Output lines start with "console:".
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "panel_registry.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace {

const char* exitName(pipeline::ExitState s) {
  switch (s) {
    case pipeline::ExitState::Running: return "running";
    case pipeline::ExitState::Exited: return "exited";
    case pipeline::ExitState::Crashed: return "crashed";
    case pipeline::ExitState::Hung: return "hung";
  }
  return "?";
}

struct Wait {
  std::string event;
  std::chrono::steady_clock::time_point start;
  bool active = false, timedOut = false;
};
Wait g_wait;

bool logHas(const std::string& needle) {
  for (const LogLine& l : EditorLog::instance().snapshot())
    if (l.text.find(needle) != std::string::npos) return true;
  return false;
}

// "consolewait:<what>|<timeout seconds>"; what = running | stopped | finished | crashed | hung | paused | log=<text>
bool waitSatisfied(App& a, const std::string& what) {
  if (what == "running") return a.play.status() == PlayStatus::Running;
  if (what == "stopped") return a.play.status() == PlayStatus::Stopped;
  if (what == "finished") return a.play.finished();
  if (what == "crashed") return a.play.exitState() == pipeline::ExitState::Crashed;
  if (what == "hung") return a.play.exitState() == pipeline::ExitState::Hung;
  if (what == "paused") return a.play.paused();
  if (what.rfind("log=", 0) == 0) return logHas(what.substr(4));
  return true;
}

void parseWait(const std::string& arg, std::string& what, int& timeout) {
  const size_t bar = arg.rfind('|');
  what = bar == std::string::npos ? arg : arg.substr(0, bar);
  timeout = bar == std::string::npos ? 60 : std::atoi(arg.c_str() + bar + 1);
}

}  // namespace

bool consoleUiHolds(App& a, const std::string& event) {
  if (event.rfind("consolewait:", 0) != 0) return false;
  if (!g_wait.active || g_wait.event != event) {
    g_wait.active = true;
    g_wait.timedOut = false;
    g_wait.event = event;
    g_wait.start = std::chrono::steady_clock::now();
  }
  std::string what;
  int timeout;
  parseWait(event.substr(12), what, timeout);
  if (waitSatisfied(a, what)) return false;
  if (std::chrono::steady_clock::now() - g_wait.start > std::chrono::seconds(timeout)) {
    g_wait.timedOut = true;
    return false;
  }
  return true;
}

bool consoleUiHook(App& a, const std::string& act, const std::string& arg) {
  if (act.rfind("console", 0) != 0) return false;
  EditorLog& log = EditorLog::instance();
  (void)log;
  if (act == "consolepause") { std::printf("console: pause ok=%d\n", a.play.pause() ? 1 : 0); return true; }
  if (act == "consoleresume") { std::printf("console: resume ok=%d\n", a.play.resume() ? 1 : 0); return true; }
  if (act == "consolestep") {
    const int n = arg.empty() ? 1 : std::atoi(arg.c_str());
    std::printf("console: step %d ok=%d\n", n, a.play.step(n) ? 1 : 0);
    return true;
  }
  if (act == "consolestop") { a.play.stop(); std::printf("console: stop\n"); return true; }
  if (act == "consoledebugger") { std::printf("console: relaunch-debugger ok=%d\n", a.play.relaunchWithDebugger() ? 1 : 0); return true; }
  if (act == "consolectl") { std::printf("console: ctl '%s' ok=%d\n", arg.c_str(), a.play.command(arg) ? 1 : 0); return true; }
  if (act == "consolesearch") { std::snprintf(a.logSearch, sizeof a.logSearch, "%s", arg.c_str()); return true; }
  if (act == "consolefilter") {  // consolefilter:info=0 | warn | error | editor | game | emulator
    const size_t eq = arg.find('=');
    const std::string k = arg.substr(0, eq);
    const bool v = eq == std::string::npos || arg[eq + 1] != '0';
    if (k == "info") a.showInfo = v;
    else if (k == "warn") a.showWarn = v;
    else if (k == "error") a.showError = v;
    else if (k == "editor") a.showEditor = v;
    else if (k == "game") a.showGame = v;
    else if (k == "emulator") a.showEmulator = v;
    return true;
  }
  if (act == "consolestate") {
    std::printf("console: status=%s paused=%d exit=%s code=%d detail='%s' ready_ms=%ld finished=%d ok=%d\n", playStatusName(a.play.status()),
                a.play.paused() ? 1 : 0, exitName(a.play.exitState()), a.play.exitCode(), a.play.exitDetail().c_str(), a.play.lastReadyMs(),
                a.play.finished() ? 1 : 0, a.play.lastOk() ? 1 : 0);
    std::fflush(stdout);
    return true;
  }
  if (act == "consolegrep") {  // consolegrep:<text>: how many console lines contain it
    int n = 0;
    for (const LogLine& l : EditorLog::instance().snapshot()) if (l.text.find(arg) != std::string::npos) ++n;
    std::printf("console: grep '%s' count=%d\n", arg.c_str(), n);
    return true;
  }
  if (act == "consolewait") {
    std::string what;
    int timeout;
    parseWait(arg, what, timeout);
    std::printf("console: wait %s %s\n", what.c_str(), g_wait.timedOut ? "TIMEOUT" : "done");
    std::fflush(stdout);
    g_wait.active = false;
    return true;
  }
  return false;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(console_play, "Console play (verbs)", nullptr, consoleUiHook, consoleUiHolds, 0, 60, ::rensga::editor::PanelDraw::None)

}  // namespace editor
}  // namespace rensga
