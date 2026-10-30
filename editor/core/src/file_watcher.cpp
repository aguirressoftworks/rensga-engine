#include "rensga/editor/file_watcher.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

std::map<std::string, FileWatcher::Stamp> FileWatcher::snap() const {
  std::map<std::string, Stamp> m;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir_, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (!name.empty() && name[0] == '.') {
      if (it->is_directory(ec)) it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    Stamp s;
    s.size = it->file_size(ec);
    const auto t = fs::last_write_time(it->path(), ec);
    s.mtime = ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
    m[fs::relative(it->path(), dir_, ec).generic_string()] = s;
  }
  return m;
}

std::vector<FileChange> FileWatcher::pollOnce() {
  std::map<std::string, Stamp> now = snap();
  std::vector<FileChange> out;
  for (const auto& kv : now) {
    auto it = snapshot_.find(kv.first);
    if (it == snapshot_.end()) out.push_back({FileChange::Added, kv.first});
    else if (it->second.size != kv.second.size || it->second.mtime != kv.second.mtime) out.push_back({FileChange::Modified, kv.first});
  }
  for (const auto& kv : snapshot_)
    if (!now.count(kv.first)) out.push_back({FileChange::Removed, kv.first});
  snapshot_ = std::move(now);
  return out;
}

void FileWatcher::start(int intervalMs) {
  if (thread_.joinable()) return;
  if (intervalMs <= 0) {
    const char* e = std::getenv("RENSGA_WATCH_MS");
    intervalMs = e ? std::atoi(e) : 0;
    if (intervalMs <= 0) intervalMs = 1000;
  }
  stop_ = false;
  thread_ = std::thread([this, intervalMs] {
    std::unique_lock<std::mutex> lk(mu_);
    while (!stop_) {
      cv_.wait_for(lk, std::chrono::milliseconds(intervalMs), [this] { return stop_; });
      if (stop_) break;
      lk.unlock();
      std::vector<FileChange> c = pollOnce();  // only this thread touches snapshot_ while it runs
      lk.lock();
      pending_.insert(pending_.end(), c.begin(), c.end());
    }
  });
}

void FileWatcher::stop() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

bool FileWatcher::take(std::vector<FileChange>& out) {
  std::lock_guard<std::mutex> lk(mu_);
  if (pending_.empty()) return false;
  out.insert(out.end(), pending_.begin(), pending_.end());
  pending_.clear();
  return true;
}

// ---- AssetSync ----
void AssetSync::open(const fs::path& projectRoot, const std::string& target) {
  stop();
  target_ = target;
  db_.open(projectRoot);
  importer_.open(projectRoot);
  outcomes_.clear();
  generation_ = importCount_ = 0;
  db_.scan();
  watcher_ = std::make_unique<FileWatcher>(projectRoot / "Assets");
}

void AssetSync::start(int ms) { if (watcher_) watcher_->start(ms); }
void AssetSync::stop() { if (watcher_) watcher_->stop(); }

const ImportOutcome* AssetSync::outcome(const std::string& guid) const {
  auto it = outcomes_.find(guid);
  return it == outcomes_.end() ? nullptr : &it->second;
}

const ImportOutcome& AssetSync::importAsset(const AssetEntry& e, bool log) {
  ImportOutcome r = importer_.import(e, target_, db_.effectiveSettings(e.guid, target_), true);
  if (log) {
    if (r.ok) EditorLog::instance().info("asset: reimported " + e.rel);
    else EditorLog::instance().error("asset: import failed for " + e.rel + ": " + r.error);
  }
  if (outcomes_.count(e.guid)) ++importCount_;
  ++generation_;
  return outcomes_[e.guid] = std::move(r);
}

void AssetSync::importMissing() {
  for (const AssetEntry& e : db_.entries())
    if ((e.kind == AssetKind::Texture || e.kind == AssetKind::Audio) && !outcomes_.count(e.guid)) importAsset(e, false);
}

SyncResult AssetSync::syncNow(bool log) {
  SyncResult r;
  r.scan = db_.scan();
  for (const std::string& rel : r.scan.removed) (void)rel;
  std::vector<std::string> todo = r.scan.created;
  todo.insert(todo.end(), r.scan.changed.begin(), r.scan.changed.end());
  std::sort(todo.begin(), todo.end());
  todo.erase(std::unique(todo.begin(), todo.end()), todo.end());
  for (const std::string& rel : todo) {
    const AssetEntry* e = db_.findPath(rel);
    if (!e || (e->kind != AssetKind::Texture && e->kind != AssetKind::Audio)) continue;
    const AssetEntry copy = *e;
    const ImportOutcome& o = importAsset(copy, log);
    if (o.ok) r.reimported.push_back(rel);
    else r.failed.emplace_back(rel, o.error);
  }
  // an asset that vanished loses its cached outcome
  for (auto it = outcomes_.begin(); it != outcomes_.end();)
    it = db_.findGuid(it->first) ? std::next(it) : outcomes_.erase(it);
  if (!r.scan.removed.empty() || !r.scan.moved.empty()) ++generation_;
  if (log) {
    for (const auto& s : r.scan.created) EditorLog::instance().info("asset: new " + s + " (meta created)");
    for (const auto& m : r.scan.moved) EditorLog::instance().info("asset: moved " + m.first + " -> " + m.second);
    for (const auto& s : r.scan.removed) EditorLog::instance().info("asset: removed " + s);
    for (const auto& s : r.scan.regenerated) EditorLog::instance().warn("asset: duplicate GUID, " + s + " got a new one");
    for (const auto& s : r.scan.errors) EditorLog::instance().warn("asset: " + s);
  }
  return r;
}

bool AssetSync::pump(SyncResult& out) {
  if (!watcher_) return false;
  std::vector<FileChange> c;
  if (!watcher_->take(c)) return false;
  out = syncNow(true);
  return out.any();
}

}  // namespace editor
}  // namespace rensga
