// Picks where a build tool runs: a native build support module (the default users get), Docker (contributor
// fallback) or neither. Never names a console: callers pass their module id and docker wrapper script.
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "rensga/modules/modules.h"
#include "rensga/pipeline/process.h"

namespace rensga {
namespace pipeline {

enum class ToolchainMode { Auto, Native, Docker };
enum class ToolchainBackend { None, Native, Docker };

// RENSGA_TOOLCHAIN=auto (default) | native | docker. Anything else is treated as auto.
ToolchainMode toolchainModeFromEnv();
const char* toolchainBackendName(ToolchainBackend b);

class ToolchainRunner {
 public:
  // dockerWrapper: script that runs its arguments in the pinned image (`<wrapper> true` checks that Docker works);
  // empty = no Docker fallback for this module.
  ToolchainRunner(std::string moduleId, std::filesystem::path dockerWrapper = {});
  ToolchainRunner(modules::ModuleManager manager, std::string moduleId, std::filesystem::path dockerWrapper = {},
                  ToolchainMode mode = toolchainModeFromEnv());

  void setMode(ToolchainMode m) { mode_ = m; resolved_ = false; }
  ToolchainMode mode() const { return mode_; }
  const modules::ModuleManager& manager() const { return manager_; }

  // auto: native module if installed, else Docker if usable, else error. native / docker: only that one.
  // On failure error is a plain message that names the module and how to install it.
  bool resolve(std::string& error);
  ToolchainBackend backend() const { return backend_; }

  // Process for `cmd` on the resolved backend (resolve() must have succeeded). cwd is where the command runs;
  // extraEnv is added. Docker mode sends the command through the wrapper, native mode adds the module env.
  ProcessSpec spec(std::vector<std::string> cmd, const std::filesystem::path& cwd,
                   std::vector<std::pair<std::string, std::string>> extraEnv = {}) const;

 private:
  bool dockerUsable();
  modules::ModuleManager manager_;
  std::string moduleId_;
  std::filesystem::path dockerWrapper_;
  ToolchainMode mode_;
  ToolchainBackend backend_ = ToolchainBackend::None;
  bool resolved_ = false;
  int dockerState_ = -1;  // -1 unknown, 0 no, 1 yes
};

}  // namespace pipeline
}  // namespace rensga
