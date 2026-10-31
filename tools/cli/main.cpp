// `rensga` CLI (MVP-38): thin wrapper over tools/pipeline. Never names a console: the target id comes
// from --target or ProjectSettings/Project.json.
//   rensga validate|cook|build|play [--target ID] [--json] [--quit-after N] [--headless] [--debugger] [--hang-timeout N] [--stdin-control] [project]
// --json prints one JSON object per line: pipeline events and {"type":"gamelog","line":...}.
// play --stdin-control reads commands from stdin, one per line: pause | resume | step [N] | stop | ctl <raw dev-control command>.
// play ends with {"type":"exit","state":"exited|crashed|hung","code":N,"signal":N,"detail":"..."}; a hung game is stopped after the report.
// Exit code: 0 ok, 1 pipeline failure, 2 usage error, 3 the game crashed, 4 the game hung.
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <deque>
#include <thread>

#include <nlohmann/json.hpp>

#include "modules_cmd.h"
#include "rensga/pipeline/emulator_launcher.h"
#include "rensga/pipeline/pipeline.h"

using namespace rensga::pipeline;

static void usage() {
  std::fprintf(stderr,
               "usage: rensga <validate|cook|build|play> [--target ID] [--json] [--quit-after SECONDS] [project]\n"
               "       play options: --headless (no emulator window) --debugger (emulator debugger on) --hang-timeout SECONDS (0 = off)\n"
               "                     --stdin-control (pause | resume | step [N] | stop | ctl <raw> lines on stdin)\n"
               "       rensga modules <list|install|remove|verify|env|path|doctor> ...   (build support modules)\n"
               "  validate  check the project and its budgets (cooks into the cache)\n"
               "  cook      validate + cook the asset pack\n"
               "  build     release build: cook, compile, package (EBOOT.PBP)\n"
               "  play      dev build and launch in the emulator\n");
}

int main(int argc, char** argv) {
  if (argc < 2 || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h")) {
    usage();
    return argc < 2 ? 2 : 0;
  }
  std::string cmd = argv[1];
  if (cmd == "modules") return runModulesCommand(argc - 2, argv + 2);
  BuildRequest req;
  bool json = false, stdinControl = false;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--target" && i + 1 < argc) req.target = argv[++i];
    else if (a == "--json") json = true;
    else if (a == "--quit-after" && i + 1 < argc) req.quitAfterSeconds = std::atoi(argv[++i]);
    else if (a == "--release") req.release = true;
    else if (a == "--headless") req.headless = true;
    else if (a == "--debugger") req.debugger = true;
    else if (a == "--hang-timeout" && i + 1 < argc) req.hangTimeoutSeconds = std::atoi(argv[++i]);
    else if (a == "--stdin-control") stdinControl = true;
    else if (!a.empty() && a[0] != '-' && req.project.empty()) req.project = a;
    else { std::fprintf(stderr, "rensga: unknown argument '%s'\n", a.c_str()); usage(); return 2; }
  }
  if (req.project.empty()) req.project = ".";
  if (cmd == "validate") req.lastStage = Stage::Cook;  // budgets are validated by the cook
  else if (cmd == "cook") req.lastStage = Stage::Cook;
  else if (cmd == "build") { req.release = true; req.lastStage = Stage::Package; }
  else if (cmd == "play") { req.release = false; req.launch = true; }
  else { std::fprintf(stderr, "rensga: unknown command '%s'\n", cmd.c_str()); usage(); return 2; }

  auto print = [&](const Event& e) {
    if (json) {
      nlohmann::json j;
      j["type"] = "event";
      j["stage"] = stageName(e.stage);
      j["status"] = statusName(e.status);
      j["message"] = e.message;
      if (!e.assetGuid.empty()) j["assetGuid"] = e.assetGuid;
      std::printf("%s\n", j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    } else {
      std::printf("[%s] %s: %s%s%s\n", stageName(e.stage), statusName(e.status), e.message.c_str(),
                  e.assetGuid.empty() ? "" : " guid=", e.assetGuid.c_str());
    }
    std::fflush(stdout);
  };

  Session session;
  bool ok = session.run(req, print);
  if (ok && req.launch) {
    auto drain = [&]() {
      for (const std::string& l : session.drainGameLog()) {
        if (json) {
          nlohmann::json j;
          j["type"] = "gamelog";
          j["line"] = l;
          std::printf("%s\n", j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
        } else {
          std::printf("[game] %s\n", l.c_str());
        }
      }
      std::fflush(stdout);
    };
    static std::mutex qm;  // static: the stdin reader thread outlives this scope
    static std::deque<std::string> cmds;
    std::thread reader;
    if (stdinControl) {
      reader = std::thread([]() {  // raw read(2): a thread blocked inside std::cin would hold stdin's lock and stall exit()
        std::string line;
        char c;
        while (read(0, &c, 1) == 1) {
          if (c != '\n') { line.push_back(c); continue; }
          std::lock_guard<std::mutex> g(qm);
          cmds.push_back(line);
          line.clear();
        }
      });
      reader.detach();
    }
    auto report = [&](const char* what, const std::string& detail) {
      if (json) {
        nlohmann::json j;
        j["type"] = "control";
        j["command"] = what;
        j["detail"] = detail;
        std::printf("%s\n", j.dump().c_str());
      } else {
        std::printf("[control] %s %s\n", what, detail.c_str());
      }
      std::fflush(stdout);
    };
    bool hungReported = false;
    int verdict = 0;
    while (session.running()) {
      drain();
      for (;;) {
        std::string c;
        {
          std::lock_guard<std::mutex> g(qm);
          if (cmds.empty()) break;
          c = cmds.front();
          cmds.pop_front();
        }
        if (c == "pause") report("pause", session.pause() ? "ok" : "failed");
        else if (c == "resume") report("resume", session.resume() ? "ok" : "failed");
        else if (c.rfind("step", 0) == 0) report(c.c_str(), session.step(c.size() > 5 ? std::atoi(c.c_str() + 5) : 1) ? "ok" : "failed");
        else if (c.rfind("ctl ", 0) == 0) report(c.c_str(), session.command(c.substr(4)) ? "ok" : "failed");
        else if (c == "stop") { report("stop", "ok"); session.stop(); }
      }
      const ExitState st = session.exitState();
      if ((st == ExitState::Hung && !hungReported) || st == ExitState::Crashed) {  // Crashed while alive = a guest CPU exception
        hungReported = true;
        verdict = st == ExitState::Hung ? 4 : 3;
        session.saveLog(req.project / "Library" / "Logs" / "last-play.log");
        drain();
        break;  // a hung or crashed game is reported and stopped
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    drain();
    const ExitState st = session.exitState();
    const std::string detail = session.exitDetail();
    if (st == ExitState::Crashed) verdict = 3;
    if (json) {
      nlohmann::json j;
      j["type"] = "exit";
      j["state"] = st == ExitState::Crashed ? "crashed" : st == ExitState::Hung ? "hung" : "exited";
      j["code"] = session.exitCode();
      j["signal"] = session.exitSignal();
      j["detail"] = detail;
      std::printf("%s\n", j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    } else {
      std::printf("[play] %s\n", detail.empty() ? "session ended" : detail.c_str());
    }
    std::fflush(stdout);
    session.stop();
    if (verdict) return verdict;
  }
  return ok ? 0 : 1;
}
