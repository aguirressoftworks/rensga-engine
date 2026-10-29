#include "rensga/editor/script_build.h"

#include <cstdio>
#include <fstream>
#include <regex>

#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

DiagLevel parseCompilerLine(const std::string& line, const fs::path& project, std::string& text) {
  static const std::regex re(R"(^(.+?):(\d+):(?:\d+:)?\s*(fatal error|error|warning):\s*(.*)$)");
  std::smatch m;
  if (!std::regex_match(line, m, re)) {
    if (line.find("undefined reference") != std::string::npos || line.find("ld returned") != std::string::npos) {
      text = line;
      return DiagLevel::Error;
    }
    return DiagLevel::None;
  }
  std::string file = m[1].str();
  std::error_code ec;
  fs::path rel = fs::relative(fs::path(file), project, ec);
  if (!ec && !rel.empty() && *rel.begin() != "..") file = rel.string();
  text = file + ":" + m[2].str() + ": " + m[4].str();
  return m[3].str() == "warning" ? DiagLevel::Warning : DiagLevel::Error;
}

std::string cmakeCacheValue(const fs::path& buildDir, const std::string& key) {
  std::ifstream in(buildDir / "CMakeCache.txt");
  std::string line;
  const std::string pre = key + ":";
  while (std::getline(in, line))
    if (line.compare(0, pre.size(), pre) == 0) {
      size_t eq = line.find('=');
      return eq == std::string::npos ? "" : line.substr(eq + 1);
    }
  return "";
}

static std::string quote(const std::string& s) {
  std::string o = "'";
  for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return o + "'";
}

ScriptBuilder::~ScriptBuilder() {
  if (th_.joinable()) th_.join();
}

bool ScriptBuilder::start(const ScriptBuildConfig& cfg) {
  if (busy_.load()) return false;
  if (th_.joinable()) th_.join();
  busy_ = true;
  finished_ = false;
  ok_ = false;
  percent_ = 0;
  th_ = std::thread([this, cfg] { run(cfg); });
  return true;
}

bool ScriptBuilder::exec(const std::string& cmd, const fs::path& project) {
  EditorLog& log = EditorLog::instance();
  FILE* p = popen((cmd + " 2>&1 </dev/null").c_str(), "r");
  if (!p) { log.error("cannot start: " + cmd); return false; }
  static const std::regex pct(R"(^\[\s*(\d+)%\])");
  std::string line;
  char buf[4096];
  while (std::fgets(buf, sizeof buf, p)) {
    line = buf;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.empty()) continue;
    std::smatch m;
    if (std::regex_search(line, m, pct)) percent_ = std::stoi(m[1].str());
    std::string text;
    switch (parseCompilerLine(line, project, text)) {
      case DiagLevel::Error: log.error(text); break;
      case DiagLevel::Warning: log.warn(text); break;
      default: log.info(line); break;
    }
  }
  int rc = pclose(p);
  if (rc != 0) log.error("command failed (exit " + std::to_string(rc == -1 ? -1 : (rc >> 8) & 0xff) + "): " + cmd);
  return rc == 0;
}

void ScriptBuilder::run(ScriptBuildConfig cfg) {
  EditorLog& log = EditorLog::instance();
  bool ok = true;
  const std::string cur = cmakeCacheValue(cfg.buildDir, "RENSGA_PROJECT_DIR");
  std::error_code ec;
  if (fs::weakly_canonical(cur, ec) != fs::weakly_canonical(cfg.project, ec)) {
    log.info("scripts: reconfiguring " + cfg.buildDir.string() + " for " + cfg.project.string());
    ok = exec(quote(cfg.cmake) + " -S " + quote(cmakeCacheValue(cfg.buildDir, "CMAKE_HOME_DIRECTORY")) + " -B " +
                  quote(cfg.buildDir.string()) + " -DRENSGA_PROJECT_DIR=" + quote(cfg.project.string()),
              cfg.project);
  }
  if (ok) {
    std::string cmd = quote(cfg.cmake) + " --build " + quote(cfg.buildDir.string()) + " -j 8 --target";
    for (const std::string& t : cfg.targets) cmd += " " + quote(t);
    log.info("scripts: building " + cfg.buildDir.string());
    ok = exec(cmd, cfg.project);
  }
  ok_ = ok;
  finished_ = true;
  busy_ = false;
}

ScriptBuilder::Result ScriptBuilder::poll() {
  if (!finished_.load()) return Result::None;
  finished_ = false;
  if (th_.joinable()) th_.join();
  return ok_.load() ? Result::Ok : Result::Failed;
}

}  // namespace editor
}  // namespace rensga
