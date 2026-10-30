// Model behind Hub > Modules and the "module is missing" dialog (Unity Hub "Add modules"). No ImGui, no console names:
// module ids and targets are data in tools/modules/manifest.d. One background install job at a time; the UI thread only
// reads snapshots and never waits.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "rensga/modules/modules.h"

namespace rensga {
namespace editor {

enum class ModuleStatus {
  Installed,     // unpacked and recorded
  NotInstalled,  // can be downloaded for this host
  Unpublished,   // our CI builds it for this host, not released yet
  Manual,        // the user installs it and points the editor at it
  NoHost,        // no build for this host at all
  Located        // manual module with a folder the user chose
};
const char* moduleStatusWord(ModuleStatus s);   // "Installed", "Not installed", "Not published yet", "Manual", "Not available", "Located"

struct ModuleRow {
  std::string id, name, kind, target, version, licenseText, source, notice, note;
  std::vector<std::string> requires_;
  std::vector<std::string> licenseIds;   // postInstall license ids that need an accept (this module only)
  uint64_t size = 0;                     // bytes to download for this host (this module only)
  uint64_t totalSize = 0;                // ... plus dependencies that are not installed yet
  ModuleStatus status = ModuleStatus::NotInstalled;
  std::string location;                  // install root, or the located folder
  bool installable() const { return status == ModuleStatus::NotInstalled; }
};

enum class JobState { Idle, Running, Done, Failed, Cancelled };

struct JobSnapshot {
  JobState state = JobState::Idle;
  std::string moduleId, phase, detail, message;   // message = last result sentence
  uint64_t done = 0, total = 0;
  float fraction() const { return total ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f; }
};

// "165 MB", "2.0 KB". Powers of 1000, one decimal below 10.
std::string formatBytes(uint64_t n);

class ModulesModel {
 public:
  // Catalog and install dir come from the environment (RENSGA_MODULE_CATALOG, RENSGA_HOME) unless given.
  ModulesModel();
  explicit ModulesModel(modules::ModuleManager manager);
  ~ModulesModel();
  ModulesModel(const ModulesModel&) = delete;
  ModulesModel& operator=(const ModulesModel&) = delete;

  void refresh();                                  // rebuild rows (not while a job runs)
  const std::vector<ModuleRow>& rows() const { return rows_; }
  const ModuleRow* find(const std::string& id) const;
  // The toolchain module for a ConsoleTarget id (data: Module.target), nullptr when none is cataloged.
  const ModuleRow* forTarget(const std::string& targetId) const;
  // True when the target's toolchain module (or a dependency) is cataloged for this host but not installed.
  bool missingForTarget(const std::string& targetId) const;
  const modules::ModuleManager& manager() const { return mgr_; }
  const std::vector<std::string>& catalogErrors() const { return mgr_.catalog().errors(); }

  // Manual modules: folders the user located (saved by the caller in the editor settings).
  void setLocated(const std::string& id, const std::string& path);
  const std::map<std::string, std::string>& located() const { return located_; }

  // Licenses: the user ticks "I accept" per license id; install is refused before any download otherwise.
  void accept(const std::string& licenseId, bool yes);
  bool accepted(const std::string& licenseId) const { return acceptedLicenses_.count(licenseId) != 0; }
  // License ids still not accepted for id and the dependencies that would be installed with it.
  std::vector<std::string> unacceptedLicenses(const std::string& id) const;

  // Starts the background install (dependencies included). Returns false with `why` when it cannot start
  // (a job is running, unknown or unpublished module, license not accepted).
  bool startInstall(const std::string& id, std::string& why);
  void cancel();
  JobSnapshot job() const;                         // thread-safe snapshot
  bool busy() const { return running_.load(); }
  bool finishedPending() const { return finished_.load(); }   // a job ended and pump() has not seen it yet
  // Once per frame: joins a finished job and refreshes rows. True on the frame the job ended.
  bool pump();

  modules::Result remove(const std::string& id);
  modules::Result verify(const std::string& id) const;

 private:
  void rebuild();
  modules::ModuleManager mgr_;
  std::vector<ModuleRow> rows_;
  std::map<std::string, std::string> located_;
  std::set<std::string> acceptedLicenses_;
  std::thread thread_;
  std::atomic<bool> running_{false}, cancel_{false}, finished_{false};
  mutable std::mutex mu_;
  JobSnapshot job_;
  std::chrono::milliseconds throttle_{0};          // RENSGA_EDITOR_TEST_MODULE_DELAY_MS: slows progress for screenshots
};

}  // namespace editor
}  // namespace rensga
