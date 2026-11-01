#include "rensga/editor/prefab_library.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

void PrefabLibrary::open(AssetDatabase* db) {
  db_ = db;
  cache_.clear();
  ++revision_;
}

long long PrefabLibrary::stampOf(const fs::path& p) const {
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec);
  if (ec) return -1;
  const auto sz = fs::file_size(p, ec);
  return static_cast<long long>(t.time_since_epoch().count()) ^ (static_cast<long long>(sz) << 40);
}

std::string PrefabLibrary::pathOf(const std::string& guid) const {
  if (!db_) return std::string();
  const AssetEntry* e = db_->findGuid(guid);
  return e && e->kind == AssetKind::Prefab ? e->path.string() : std::string();
}

std::string PrefabLibrary::relOf(const std::string& guid) const {
  if (!db_) return std::string();
  const AssetEntry* e = db_->findGuid(guid);
  return e && e->kind == AssetKind::Prefab ? e->rel : std::string();
}

std::string PrefabLibrary::nameOf(const std::string& guid) const {
  const std::string p = pathOf(guid);
  return p.empty() ? guid : fs::path(p).stem().string();
}

std::vector<std::string> PrefabLibrary::allGuids() const {
  std::vector<std::string> v;
  if (db_) for (const AssetEntry& e : db_->entries()) if (e.kind == AssetKind::Prefab) v.push_back(e.guid);
  return v;
}

PrefabLibrary::Cached& PrefabLibrary::load(const std::string& guid) {
  Cached& c = cache_[guid];
  const std::string path = pathOf(guid);
  const long long st = path.empty() ? -1 : stampOf(path);
  if (c.stamp == st && (c.ok || !c.err.empty())) return c;
  c = Cached();
  c.stamp = st;
  if (path.empty()) { c.err = "no prefab asset has GUID " + guid; return c; }
  PrefabAsset a;
  std::string err;
  if (!loadPrefab(path, a, err)) { c.err = err; return c; }
  a.guid = guid;  // the .meta is the authority on the GUID
  c.asset = std::move(a);
  c.ok = true;
  return c;
}

bool PrefabLibrary::get(const std::string& guid, PrefabAsset& out, std::string& err) {
  Cached& c = load(guid);
  if (!c.ok) { err = c.err; return false; }
  out = c.asset;
  return true;
}

PrefabLoader PrefabLibrary::loader() {
  return [this](const std::string& guid, PrefabAsset& out, std::string& err) { return get(guid, out, err); };
}

PrefabNamer PrefabLibrary::namer() {
  return [this](const std::string& guid) { return nameOf(guid); };
}

bool PrefabLibrary::save(const std::string& guid, const PrefabAsset& a, std::string& err) {
  const std::string path = pathOf(guid);
  if (path.empty()) { err = "no prefab asset has GUID " + guid; return false; }
  PrefabAsset w = a;
  w.guid = guid;
  if (!savePrefab(path, w, err)) return false;
  Cached& c = cache_[guid];
  c.asset = std::move(w);
  c.ok = true;
  c.err.clear();
  c.stamp = stampOf(path);
  ++revision_;
  return true;
}

std::string PrefabLibrary::readText(const std::string& guid) const {
  const std::string path = pathOf(guid);
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool PrefabLibrary::writeText(const std::string& guid, const std::string& text, std::string& err) {
  const std::string path = pathOf(guid);
  if (path.empty()) { err = "no prefab asset has GUID " + guid; return false; }
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) { err = "cannot write " + tmp; return false; }
    f << text;
    if (!f) { err = "write failed: " + tmp; return false; }
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) { err = "cannot replace " + path; return false; }
  cache_.erase(guid);
  ++revision_;
  return true;
}

bool PrefabLibrary::poll(double nowSec, double minIntervalSec) {
  if (!db_ || nowSec - lastPoll_ < minIntervalSec) return false;
  lastPoll_ = nowSec;
  bool changed = false;
  for (auto& kv : cache_) {
    const std::string path = pathOf(kv.first);
    const long long st = path.empty() ? -1 : stampOf(path);
    if (st != kv.second.stamp) { kv.second.stamp = -2; changed = true; }  // forces a reload on the next get()
  }
  if (changed) ++revision_;
  return changed;
}

std::string PrefabLibrary::wouldCycle(const std::string& container, const std::string& inner) {
  if (container == inner) return nameOf(container) + " cannot contain itself";
  // does `inner` (through nesting or variant bases) already reach `container`?
  std::vector<std::string> stack{inner}, seen;
  while (!stack.empty()) {
    const std::string g = stack.back();
    stack.pop_back();
    if (std::find(seen.begin(), seen.end(), g) != seen.end()) continue;
    seen.push_back(g);
    PrefabAsset a;
    std::string err;
    if (!get(g, a, err)) continue;
    std::vector<std::string> refs;
    if (a.isVariant()) refs.push_back(a.variantBase);
    for (const PrefabObject& o : a.objects) if (!o.nestedAsset.empty()) refs.push_back(o.nestedAsset);
    for (const std::string& r : refs) {
      if (r == container) return "prefab cycle: " + nameOf(container) + " -> " + nameOf(inner) + " -> ... -> " + nameOf(container);
      stack.push_back(r);
    }
  }
  return std::string();
}

}  // namespace editor
}  // namespace rensga
