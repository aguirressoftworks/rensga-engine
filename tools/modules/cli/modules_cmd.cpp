// `rensga modules ...`: install and inspect build support modules (tools/modules). Never names a console.
#include "modules_cmd.h"

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/modules/modules.h"

using namespace rensga::modules;
using nlohmann::json;

namespace {

void usage() {
  std::fprintf(stderr,
               "usage: rensga modules <command>\n"
               "  list [--json]                     modules, their state on this host\n"
               "  install <id>... [--accept-licenses]  download, verify and install (dependencies first)\n"
               "  remove <id>                       delete an installed module\n"
               "  verify <id>                       run the module's check command\n"
               "  env <id> [--shell sh|cmd|json]    environment for building with the module\n"
               "  path <id>                         install directory\n"
               "  doctor [--json]                   host, folders, helper tools, module states\n"
               "environment: RENSGA_HOME (modules go to $RENSGA_HOME/modules), RENSGA_MODULE_CATALOG\n");
}

std::string stateOf(const ModuleManager& mm, const Module& m) {
  if (mm.isInstalled(m.id)) return "installed";
  switch (mm.availability(m.id)) {
    case Availability::Available: return "available";
    case Availability::Unpublished: return "unpublished";
    case Availability::Manual: return "manual";
    case Availability::UnknownHost: return "unsupported-host";
    default: return "unknown";
  }
}

uint64_t totalSize(const ModuleManager& mm, const Module& m) {
  auto it = m.hosts.find(mm.host());
  uint64_t n = 0;
  if (it != m.hosts.end())
    for (const auto& p : it->second.parts) n += p.size;
  return n;
}

std::string shQuote(const std::string& s) {
  std::string o = "'";
  for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return o + "'";
}

std::string humanSize(uint64_t n) {
  char b[32];
  if (n >= (1ull << 30)) std::snprintf(b, sizeof b, "%.1f GB", double(n) / double(1ull << 30));
  else if (n >= (1ull << 20)) std::snprintf(b, sizeof b, "%.0f MB", double(n) / double(1ull << 20));
  else std::snprintf(b, sizeof b, "%llu KB", (unsigned long long)(n / 1024));
  return b;
}

ModuleManager makeManager(bool quiet = false) {
  ModuleCatalog c = ModuleCatalog::load(ModuleCatalog::defaultDir());
  if (!quiet)
    for (const auto& e : c.errors()) std::fprintf(stderr, "rensga: catalog: %s\n", e.c_str());
  return ModuleManager(std::move(c));
}

int cmdList(bool asJson) {
  ModuleManager mm = makeManager();
  json arr = json::array();
  for (const Module& m : mm.catalog().all()) {
    std::string st = stateOf(mm, m);
    if (asJson) {
      json j = {{"id", m.id}, {"name", m.name}, {"kind", m.kind}, {"target", m.target.empty() ? json(nullptr) : json(m.target)},
                {"version", m.version}, {"state", st}, {"size", totalSize(mm, m)}, {"license", m.license}, {"requires", m.requires_}};
      if (st == "installed") j["root"] = mm.root(m.id).string();
      arr.push_back(j);
    } else {
      std::printf("%-14s %-12s %-10s %-16s %s\n", m.id.c_str(), m.version.c_str(), m.kind.c_str(), st.c_str(), m.name.c_str());
    }
  }
  if (asJson) std::printf("%s\n", json{{"host", mm.host()}, {"modulesDir", mm.modulesDir().string()}, {"modules", arr}}.dump(2).c_str());
  return 0;
}

int cmdInstall(const std::vector<std::string>& ids, bool acceptAll) {
  ModuleManager mm = makeManager();
  int rc = 0;
  for (const std::string& id : ids) {
    std::string lastPhase;
    int lastPct = -10;
    ProgressFn progress = [&](const Progress& p) {
      if (p.phase == "download" && p.total) {
        int pct = int(p.done * 100 / p.total);
        if (pct >= lastPct + 5 || p.phase != lastPhase) {
          std::fprintf(stderr, "[%s] download %d%% (%s / %s) %s\n", p.moduleId.c_str(), pct, humanSize(p.done).c_str(), humanSize(p.total).c_str(), p.detail.c_str());
          lastPct = pct;
        }
      } else if (p.phase != lastPhase || p.phase == "extract") {
        if (p.phase != "done") std::fprintf(stderr, "[%s] %s %s\n", p.moduleId.c_str(), p.phase.c_str(), p.detail.c_str());
      }
      lastPhase = p.phase;
    };
    LicenseFn lic = [&](const std::string& license, const Module& m) {
      if (acceptAll) {
        std::fprintf(stderr, "[%s] license '%s' accepted (--accept-licenses)\n", m.id.c_str(), license.c_str());
        return true;
      }
      if (!isatty(0)) return false;
      std::fprintf(stderr, "%s needs you to accept the license '%s'.\n%s\nAccept? [y/N] ", m.name.c_str(), license.c_str(), m.notice.c_str());
      std::string a;
      std::getline(std::cin, a);
      return a == "y" || a == "Y" || a == "yes";
    };
    Result r = mm.install(id, progress, nullptr, lic);
    std::printf("%s\n", r.message.c_str());
    if (!r.ok) rc = 1;
  }
  return rc;
}

int cmdEnv(const std::string& id, const std::string& shell) {
  ModuleManager mm = makeManager();
  ModuleEnv env;
  std::string err;
  if (!mm.envFor(id, env, &err)) {
    std::fprintf(stderr, "rensga: %s\n", err.c_str());
    return 1;
  }
  if (shell == "json") {
    json vars = json::object();
    for (const auto& kv : env.vars) vars[kv.first] = kv.second;
    std::printf("%s\n", json{{"vars", vars}, {"path", env.pathDirs}}.dump(2).c_str());
  } else if (shell == "cmd") {
    for (const auto& kv : env.vars) std::printf("set \"%s=%s\"\n", kv.first.c_str(), kv.second.c_str());
    if (!env.pathDirs.empty()) {
      std::string p;
      for (const auto& d : env.pathDirs) p += d + ";";
      std::printf("set \"PATH=%s%%PATH%%\"\n", p.c_str());
    }
  } else {
    for (const auto& kv : env.vars) std::printf("export %s=%s\n", kv.first.c_str(), shQuote(kv.second).c_str());
    if (!env.pathDirs.empty()) {
      std::string p;
      for (const auto& d : env.pathDirs) p += (p.empty() ? "" : ":") + d;
      std::printf("export PATH=%s:\"$PATH\"\n", shQuote(p).c_str());
    }
  }
  return 0;
}

int cmdDoctor(bool asJson) {
  ModuleManager mm = makeManager(true);
  ModuleCatalog cat = ModuleCatalog::load(ModuleCatalog::defaultDir());
  auto tools = mm.probeHostTools();
  json j;
  j["host"] = mm.host();
  j["modulesDir"] = mm.modulesDir().string();
  j["catalogDir"] = ModuleCatalog::defaultDir().string();
  j["catalogErrors"] = cat.errors();
  j["tools"] = json::array();
  for (const auto& t : tools) j["tools"].push_back({{"name", t.name}, {"found", t.found}, {"path", t.path}});
  j["modules"] = json::array();
  for (const Module& m : mm.catalog().all()) {
    std::string why;
    mm.availability(m.id, &why);
    j["modules"].push_back({{"id", m.id}, {"state", stateOf(mm, m)}, {"why", why}});
  }
  bool curl = false, unpack = false;
  for (const auto& t : tools) {
    if (t.name == "curl" && t.found) curl = true;
    if (t.name == "tar" && t.found) unpack = true;
  }
  j["ok"] = curl && unpack && cat.errors().empty();
  if (asJson) {
    std::printf("%s\n", j.dump(2).c_str());
  } else {
    std::printf("host:        %s\nmodules dir: %s\ncatalog:     %s\n", mm.host().c_str(), mm.modulesDir().string().c_str(), ModuleCatalog::defaultDir().string().c_str());
    for (const auto& e : cat.errors()) std::printf("catalog problem: %s\n", e.c_str());
    for (const auto& t : tools) std::printf("tool %-7s %s\n", t.name.c_str(), t.found ? t.path.c_str() : "not found");
    if (!curl) std::printf("problem: curl is missing; modules cannot be downloaded (Windows 10+ ships curl.exe).\n");
    if (!unpack) std::printf("problem: tar is missing; modules cannot be unpacked.\n");
    for (const Module& m : mm.catalog().all()) std::printf("module %-14s %s\n", m.id.c_str(), stateOf(mm, m).c_str());
    std::printf("%s\n", j["ok"].get<bool>() ? "ok" : "problems found");
  }
  return j["ok"].get<bool>() ? 0 : 1;
}

}  // namespace

int runModulesCommand(int argc, char** argv) {
  if (argc < 1 || !std::strcmp(argv[0], "--help") || !std::strcmp(argv[0], "-h")) {
    usage();
    return argc < 1 ? 2 : 0;
  }
  std::string cmd = argv[0];
  std::vector<std::string> pos;
  bool asJson = false, acceptAll = false;
  std::string shell = "sh";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--json") asJson = true;
    else if (a == "--accept-licenses") acceptAll = true;
    else if (a == "--shell" && i + 1 < argc) shell = argv[++i];
    else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "rensga: unknown argument '%s'\n", a.c_str()); usage(); return 2; }
    else pos.push_back(a);
  }
  if (cmd == "list") return cmdList(asJson);
  if (cmd == "doctor") return cmdDoctor(asJson);
  if (pos.empty()) { std::fprintf(stderr, "rensga: modules %s needs a module id\n", cmd.c_str()); usage(); return 2; }
  if (cmd == "install") return cmdInstall(pos, acceptAll);
  if (cmd == "env") {
    if (shell != "sh" && shell != "cmd" && shell != "json") { std::fprintf(stderr, "rensga: --shell must be sh, cmd or json\n"); return 2; }
    return cmdEnv(pos[0], shell);
  }
  ModuleManager mm = makeManager();
  if (cmd == "remove" || cmd == "verify") {
    Result r = cmd == "remove" ? mm.remove(pos[0]) : mm.verify(pos[0]);
    std::printf("%s\n", r.message.c_str());
    return r.ok ? 0 : 1;
  }
  if (cmd == "path") {
    if (!mm.isInstalled(pos[0])) {
      const Module* m = mm.find(pos[0]);
      std::fprintf(stderr, "rensga: %s is not installed. Install it from Hub > Modules or run: rensga modules install %s\n", m ? m->name.c_str() : pos[0].c_str(), pos[0].c_str());
      return 1;
    }
    std::printf("%s\n", mm.root(pos[0]).string().c_str());
    return 0;
  }
  std::fprintf(stderr, "rensga: unknown modules command '%s'\n", cmd.c_str());
  usage();
  return 2;
}
