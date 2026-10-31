// rensga-cooker: cook a project for a target, or generate console_target.gen.h.
//   rensga-cooker cook --project DIR [--target ID] [--force] [--gen-header FILE]   (no --target = the project's profile)
//   rensga-cooker gen-header --project DIR --out FILE
// Exit code 0 on success, 1 on cook/budget errors, 2 on usage errors.
#include <cstdio>
#include <cstring>
#include <fstream>

#include "rensga/cooker/cooker.h"

using namespace rensga;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: rensga-cooker cook|gen-header --project DIR [options]\n");
    return 2;
  }
  std::string cmd = argv[1];
  CookOptions opt;
  std::filesystem::path out;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--project") opt.project = val();
    else if (a == "--target") opt.target = val();
    else if (a == "--force") opt.force = true;
    else if (a == "--gen-header") opt.genHeader = val();
    else if (a == "--out") out = val();
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  if (opt.project.empty()) { std::fprintf(stderr, "--project is required\n"); return 2; }
  if (cmd == "gen-header") {
    try {
      ConsoleTargetData t = loadConsoleTarget(opt.project / "ProjectSettings" / "ConsoleTarget.json");
      std::string h = generateConsoleTargetHeader(t);
      if (out.empty()) std::fputs(h.c_str(), stdout);
      else {
        if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path());
        std::ofstream(out, std::ios::binary) << h;
      }
      return 0;
    } catch (const std::exception& e) {
      std::fprintf(stderr, "error: %s\n", e.what());
      return 1;
    }
  }
  if (cmd != "cook") { std::fprintf(stderr, "unknown command %s\n", cmd.c_str()); return 2; }
  CookResult r = cookProject(opt, [](const CookMessage& m) {
    const char* tag = m.severity == CookMessage::Error ? "error" : (m.severity == CookMessage::Warning ? "warning" : "info");
    std::fprintf(m.severity == CookMessage::Error ? stderr : stdout, "rensga-cooker: %s: %s\n", tag, m.text.c_str());
  });
  return r.ok ? 0 : 1;
}
