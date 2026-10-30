#include "rensga/pipeline/toolchain.h"

#include <cstdlib>

namespace rensga {
namespace pipeline {

ToolchainMode toolchainModeFromEnv() {
  const char* e = std::getenv("RENSGA_TOOLCHAIN");
  std::string v = e ? e : "";
  if (v == "native") return ToolchainMode::Native;
  if (v == "docker") return ToolchainMode::Docker;
  return ToolchainMode::Auto;
}

const char* toolchainBackendName(ToolchainBackend b) {
  switch (b) {
    case ToolchainBackend::Native: return "native";
    case ToolchainBackend::Docker: return "docker";
    default: return "none";
  }
}

ToolchainRunner::ToolchainRunner(std::string moduleId, std::filesystem::path dockerWrapper)
    : ToolchainRunner(modules::ModuleManager(modules::ModuleCatalog::load(modules::ModuleCatalog::defaultDir())),
                      std::move(moduleId), std::move(dockerWrapper)) {}

ToolchainRunner::ToolchainRunner(modules::ModuleManager manager, std::string moduleId, std::filesystem::path dockerWrapper,
                                 ToolchainMode mode)
    : manager_(std::move(manager)), moduleId_(std::move(moduleId)), dockerWrapper_(std::move(dockerWrapper)), mode_(mode) {}

bool ToolchainRunner::dockerUsable() {
  if (dockerState_ < 0) {
    dockerState_ = 0;
    std::error_code ec;
    if (!dockerWrapper_.empty() && std::filesystem::exists(dockerWrapper_, ec)) {
      ProcessSpec ps;
      ps.argv = {dockerWrapper_.string(), "true"};
      dockerState_ = runProcess(ps, nullptr, nullptr) == 0 ? 1 : 0;
    }
  }
  return dockerState_ == 1;
}

bool ToolchainRunner::resolve(std::string& error) {
  backend_ = ToolchainBackend::None;
  resolved_ = false;
  const modules::Module* m = manager_.find(moduleId_);
  const std::string name = m ? m->name : moduleId_;
  std::string nativeProblem;
  if (mode_ != ToolchainMode::Docker) {
    modules::ModuleEnv env;
    if (manager_.envFor(moduleId_, env, &nativeProblem)) {
      backend_ = ToolchainBackend::Native;
      resolved_ = true;
      return true;
    }
    // Not installed: say why it cannot be installed here when that is the reason (unpublished host, manual module).
    std::string why;
    if (m && manager_.availability(moduleId_, &why) != modules::Availability::Available) nativeProblem = why;
  }
  if (mode_ != ToolchainMode::Native) {
    if (dockerUsable()) {
      backend_ = ToolchainBackend::Docker;
      resolved_ = true;
      return true;
    }
  }
  if (mode_ == ToolchainMode::Docker) {
    error = "RENSGA_TOOLCHAIN=docker was requested for " + name + ", but Docker is not usable" +
            (dockerWrapper_.empty() ? " (this module has no Docker image)." : " (" + dockerWrapper_.string() + " true failed).");
  } else {
    error = nativeProblem.empty() ? name + " is not installed. Install it from Hub > Modules or run: rensga modules install " + moduleId_
                                  : nativeProblem;
    if (mode_ == ToolchainMode::Auto && !dockerWrapper_.empty() && !m) error += " (Docker is not usable either.)";
  }
  return false;
}

ProcessSpec ToolchainRunner::spec(std::vector<std::string> cmd, const std::filesystem::path& cwd,
                                  std::vector<std::pair<std::string, std::string>> extraEnv) const {
  ProcessSpec ps;
  ps.cwd = cwd;
  if (backend_ == ToolchainBackend::Docker) {
    ps.argv.push_back(dockerWrapper_.string());
    for (auto& c : cmd) ps.argv.push_back(std::move(c));
    ps.env = std::move(extraEnv);
    return ps;
  }
  ps.argv = std::move(cmd);
  modules::ModuleEnv env;
  std::string err;
  if (backend_ == ToolchainBackend::Native && manager_.envFor(moduleId_, env, &err)) {
    const char* inherited = std::getenv("PATH");
    ps.env = env.flatten(inherited ? inherited : "");
  }
  for (auto& kv : extraEnv) ps.env.push_back(std::move(kv));
  return ps;
}

}  // namespace pipeline
}  // namespace rensga
