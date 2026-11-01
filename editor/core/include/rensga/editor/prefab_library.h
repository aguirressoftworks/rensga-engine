// F6.01 (Editor 6): the prefab files of the open project, by asset GUID. Caches the parsed files, notices changes on disk
// (revision counter) and writes them atomically. No ImGui, no console names.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "rensga/cooker/prefab_expand.h"
#include "rensga/editor/asset_database.h"
#include "rensga/editor/prefab_format.h"

namespace rensga {
namespace editor {

class PrefabLibrary {
 public:
  // The database must outlive the library. Prefab paths come from it (GUID -> file), so a rename or move never breaks an instance.
  void open(AssetDatabase* db);
  bool isOpen() const { return db_ != nullptr; }
  AssetDatabase* database() { return db_; }

  bool get(const std::string& guid, PrefabAsset& out, std::string& err);     // cached
  std::string pathOf(const std::string& guid) const;                           // absolute, "" if unknown
  std::string relOf(const std::string& guid) const;                            // "Assets/Enemies/Enemy.prefab"
  std::string nameOf(const std::string& guid) const;                           // file stem, or the GUID when unknown
  std::vector<std::string> allGuids() const;                                   // every .prefab asset
  PrefabLoader loader();
  PrefabNamer namer();

  // Writes the file (atomic), refreshes the cache and bumps the revision. `err` on failure.
  bool save(const std::string& guid, const PrefabAsset& a, std::string& err);
  // Exact file text, for undo steps that restore a prefab file.
  std::string readText(const std::string& guid) const;
  bool writeText(const std::string& guid, const std::string& text, std::string& err);

  // Revision counter: grows whenever a prefab's content changed (save, writeText, or a change on disk seen by poll()).
  int revision() const { return revision_; }
  // Looks at the cached files' modification times (at most every `minIntervalSec`); true if something changed.
  bool poll(double nowSec, double minIntervalSec = 0.4);
  void forget(const std::string& guid) { cache_.erase(guid); ++revision_; }

  // Would `inner` placed inside `container` (as nested prefab, variant base) create a cycle? Returns "" or a message.
  std::string wouldCycle(const std::string& container, const std::string& inner);

 private:
  struct Cached {
    PrefabAsset asset;
    bool ok = false;
    std::string err;
    long long stamp = 0;     // mtime ticks + size
  };
  long long stampOf(const std::filesystem::path& p) const;
  Cached& load(const std::string& guid);

  AssetDatabase* db_ = nullptr;
  std::map<std::string, Cached> cache_;
  int revision_ = 0;
  double lastPoll_ = -100;
};

}  // namespace editor
}  // namespace rensga
