// F3.02: file watching and the live asset sync. FileWatcher is a portable polling watcher (size + mtime, about once a
// second on its own thread; RENSGA_WATCH_MS overrides the interval). AssetSync turns its reports into scans of the asset
// database and reimports of the changed textures and audio through the cooker's per-asset cache.
// No ImGui, no console names.
#pragma once
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rensga/editor/asset_database.h"
#include "rensga/editor/import_settings.h"

namespace rensga {
namespace editor {

struct FileChange {
  enum Kind { Added, Modified, Removed } kind = Modified;
  std::string rel;  // relative to the watched directory, '/' separators
};

class FileWatcher {
 public:
  explicit FileWatcher(std::filesystem::path dir) : dir_(std::move(dir)) { snapshot_ = snap(); }
  ~FileWatcher() { stop(); }
  // Starts the polling thread. intervalMs 0 = RENSGA_WATCH_MS, else 1000.
  void start(int intervalMs = 0);
  void stop();
  // Compares the directory with the previous snapshot. Call it directly only while the thread is not running.
  std::vector<FileChange> pollOnce();
  // Moves the changes the thread has seen so far into `out`. Returns true when there were any.
  bool take(std::vector<FileChange>& out);

 private:
  struct Stamp { uint64_t size = 0; int64_t mtime = 0; };
  std::map<std::string, Stamp> snap() const;
  std::filesystem::path dir_;
  std::map<std::string, Stamp> snapshot_;
  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::vector<FileChange> pending_;
  bool stop_ = false;
};

struct SyncResult {
  ScanReport scan;
  std::vector<std::string> reimported;          // rel paths imported this time
  std::vector<std::pair<std::string, std::string>> failed;  // rel, message
  bool any() const { return scan.any() || !reimported.empty() || !failed.empty(); }
};

class AssetSync {
 public:
  // `target` is the project's console target id: reimports cook for it.
  void open(const std::filesystem::path& projectRoot, const std::string& target);
  void start(int intervalMs = 0);
  void stop();
  AssetDatabase& db() { return db_; }
  const AssetDatabase& db() const { return db_; }
  const AssetImporter& importer() const { return importer_; }
  const std::string& target() const { return target_; }

  // Scans now and reimports what changed. log: true prints "asset: reimported <path>" to the editor log.
  SyncResult syncNow(bool log = true);
  // Main thread, once per frame: when the watcher saw something, syncNow. Returns true when assets changed.
  bool pump(SyncResult& out);
  // Imports one asset (settings from its .meta) for the project's target and keeps the outcome.
  const ImportOutcome& importAsset(const AssetEntry& e, bool log);
  // Last outcome for a GUID (nullptr if never imported). Texture and audio only.
  const ImportOutcome* outcome(const std::string& guid) const;
  // Imports every texture and audio asset that has no outcome yet (cache hits are cheap).
  void importMissing();
  void forget(const std::string& guid) { outcomes_.erase(guid); }
  // Counts up whenever any outcome is replaced; thumbnails compare against it.
  int generation() const { return generation_; }
  int importCount() const { return importCount_; }  // reimports done after the first import of each asset (tests)

 private:
  AssetDatabase db_;
  AssetImporter importer_;
  std::unique_ptr<FileWatcher> watcher_;
  std::string target_;
  std::map<std::string, ImportOutcome> outcomes_;
  int generation_ = 0, importCount_ = 0;
};

}  // namespace editor
}  // namespace rensga
