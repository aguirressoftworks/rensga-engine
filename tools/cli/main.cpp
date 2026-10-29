// `rensga` CLI (MVP-38): thin wrapper over tools/pipeline. Never names a console: the target id comes
// from --target or ProjectSettings/Project.json.
//   rensga validate|cook|build|play [--target ID] [--json] [--quit-after N] [project]
// --json prints one JSON object per line: pipeline events and {"type":"gamelog","line":...}.
// Exit code: 0 ok, 1 pipeline failure, 2 usage error.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include <nlohmann/json.hpp>

#include "rensga/pipeline/pipeline.h"

using namespace rensga::pipeline;

static void usage() {
  std::fprintf(stderr,
               "usage: rensga <validate|cook|build|play> [--target ID] [--json] [--quit-after SECONDS] [project]\n"
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
  BuildRequest req;
  bool json = false;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--target" && i + 1 < argc) req.target = argv[++i];
    else if (a == "--json") json = true;
    else if (a == "--quit-after" && i + 1 < argc) req.quitAfterSeconds = std::atoi(argv[++i]);
    else if (a == "--release") req.release = true;
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
    while (session.running()) {
      drain();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    drain();
    session.stop();
  }
  return ok ? 0 : 1;
}
