// Build support modules (Unity Hub style): pinned, sha256-verified, prebuilt native toolchains installed per user.
// Host tools only (no ImGui, no console names: module ids and targets are data in manifest.d/*.json).
// Schema and install layout: tools/modules/README.md.
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rensga {
namespace modules {

// One downloadable archive. A host entry holds one (inline) or several ("parts"), each unpacked into <root>/<dest>.
struct ArchivePart {
  std::string url, sha256, archive;  // archive: tar.gz | tar.xz | zip (empty = guessed from the url)
  std::string filename;              // archive "file": the downloaded file is copied to <dest>/<filename> and made executable
  std::string dest;                  // sub-directory of the module root ("" = the root)
  uint64_t size = 0;                 // bytes, for progress; 0 = unknown
  int strip = 0;                     // leading directories dropped after unpacking
};

struct HostEntry {
  std::string status = "available";  // available | unpublished | manual
  std::string note;                  // shown with unpublished / manual
  std::vector<ArchivePart> parts;
};

struct PostInstallStep {
  std::vector<std::string> argv;  // {root} and {root:<id>} are expanded
  std::string license;            // runs only after the user accepted this license id
};

struct Module {
  std::string id, name, kind, target, version, license, source, notice;
  std::vector<std::string> requires_;
  std::map<std::string, HostEntry> hosts;
  std::vector<std::pair<std::string, std::string>> env;  // {root} expanded
  std::vector<std::string> path;                         // directories prepended to PATH
  std::vector<PostInstallStep> postInstall;
  std::vector<std::string> check;                        // command that must exit 0 after install
};

class ModuleCatalog {
 public:
  // Loads every *.json in dir, sorted by file name. Problems are collected in errors() (the good entries still load).
  static ModuleCatalog load(const std::filesystem::path& dir);
  // $RENSGA_MODULE_CATALOG, else $RENSGA_ROOT/tools/modules/manifest.d, else the source tree this binary was built from.
  static std::filesystem::path defaultDir();
  const std::vector<Module>& all() const { return modules_; }
  const Module* find(const std::string& id) const;
  const std::vector<std::string>& errors() const { return errors_; }
  void add(Module m);  // tests

 private:
  std::vector<Module> modules_;
  std::vector<std::string> errors_;
};

enum class Availability { Available, Unpublished, Manual, UnknownHost, UnknownModule };

struct Result {
  bool ok = false;
  std::string message;  // plain sentence: what happened, or why it failed
};

struct Progress {
  std::string moduleId;
  std::string phase;       // download | verify | extract | check | postinstall | done
  uint64_t done = 0;       // bytes (download) else 0
  uint64_t total = 0;      // 0 = unknown
  std::string detail;
};
using ProgressFn = std::function<void(const Progress&)>;
// Called once per license id before anything is downloaded; return true when the user accepted it.
using LicenseFn = std::function<bool(const std::string& licenseId, const Module& module)>;

// Environment a module needs: variables plus directories to prepend to PATH (dependencies included).
struct ModuleEnv {
  std::vector<std::pair<std::string, std::string>> vars;
  std::vector<std::string> pathDirs;
  // vars + PATH (pathDirs, then inheritedPath) ready for ProcessSpec::env.
  std::vector<std::pair<std::string, std::string>> flatten(const std::string& inheritedPath) const;
};

struct InstalledRecord {
  std::string id, version, host, root;
};

struct HostToolProbe {
  std::string name;   // curl, tar, unzip, ...
  bool found = false;
  std::string path;
};

class ModuleManager {
 public:
  // modulesDir empty = defaultModulesDir(). host empty = hostId(). Both are overridable for tests.
  explicit ModuleManager(ModuleCatalog catalog, std::filesystem::path modulesDir = {}, std::string host = {});

  static std::string hostId();                          // linux-x86_64 | windows-x86_64 | <os>-<arch>
  static std::filesystem::path defaultModulesDir();     // $RENSGA_HOME/modules, else the per-user data dir
  const std::filesystem::path& modulesDir() const { return dir_; }
  const std::string& host() const { return host_; }
  const ModuleCatalog& catalog() const { return catalog_; }

  const Module* find(const std::string& id) const { return catalog_.find(id); }
  std::vector<InstalledRecord> installed() const;
  bool isInstalled(const std::string& id) const;        // own record + root exists (dependencies are not checked)
  std::filesystem::path root(const std::string& id) const;  // install root of the pinned version ("" when unknown)
  Availability availability(const std::string& id, std::string* why = nullptr) const;  // for this host

  // Installs dependencies first. Order: license check, download (cached, resumable), sha256, unpack into a
  // temp dir, atomic rename into place, check command, postInstall steps. A failure or cancel leaves no partial root.
  Result install(const std::string& id, ProgressFn progress = {}, const std::atomic<bool>* cancel = nullptr,
                 LicenseFn acceptLicense = {});
  Result remove(const std::string& id);                 // refuses while an installed module requires it
  Result verify(const std::string& id) const;           // root exists and the check command exits 0
  // Variables and PATH entries of id and its dependencies (dependencies first). ok=false: not installed.
  bool envFor(const std::string& id, ModuleEnv& out, std::string* error = nullptr) const;

  std::vector<HostToolProbe> probeHostTools() const;    // curl, tar, unzip, bsdtar, cmake, docker
  std::string expand(const std::string& text, const Module& m) const;  // {root}, {root:<id>}

 private:
  Result installOne(const Module& m, const ProgressFn& progress, const std::atomic<bool>* cancel, const LicenseFn& lic);
  bool runCheck(const Module& m, std::string& output) const;
  void writeInstalled(const std::vector<InstalledRecord>& r) const;
  ModuleCatalog catalog_;
  std::filesystem::path dir_;
  std::string host_;
};

// SHA-256 of a file as lower-case hex ("" when unreadable). Own implementation, no dependency.
std::string sha256File(const std::filesystem::path& file);
std::string sha256Bytes(const std::string& data);

}  // namespace modules
}  // namespace rensga
