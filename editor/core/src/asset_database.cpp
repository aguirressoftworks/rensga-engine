#include "rensga/editor/asset_database.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

#include "rensga/cooker/assets.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

uint64_t hashText(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  return h;
}

uint64_t hashFileContent(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  uint64_t h = 1469598103934665603ull;
  char buf[65536];
  while (in) {
    in.read(buf, sizeof buf);
    for (std::streamsize i = 0; i < in.gcount(); ++i) { h ^= static_cast<unsigned char>(buf[i]); h *= 1099511628211ull; }
  }
  return h;
}

const char* assetKindName(AssetKind k) {
  switch (k) {
    case AssetKind::Folder: return "folder";
    case AssetKind::Texture: return "texture";
    case AssetKind::Audio: return "audio";
    case AssetKind::Model: return "model";
    case AssetKind::Scene: return "scene";
    case AssetKind::Script: return "script";
    case AssetKind::Primitive: return "primitive";
    case AssetKind::Tilemap: return "tilemap";
    case AssetKind::Animation: return "animation";
    case AssetKind::Font: return "font";
    case AssetKind::Data: return "data";
    default: return "other";
  }
}

static bool endsWith(const std::string& s, const char* suf) {
  const size_t n = std::char_traits<char>::length(suf);
  return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

AssetKind assetKindOf(const fs::path& file) {
  std::string n = file.filename().string();
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (endsWith(n, ".png")) return AssetKind::Texture;
  if (endsWith(n, ".wav")) return AssetKind::Audio;
  if (endsWith(n, ".gltf") || endsWith(n, ".glb")) return AssetKind::Model;
  if (endsWith(n, ".scene")) return AssetKind::Scene;
  if (endsWith(n, ".cpp") || endsWith(n, ".h")) return AssetKind::Script;
  if (endsWith(n, ".prim.json")) return AssetKind::Primitive;
  if (endsWith(n, ".tilemap.json")) return AssetKind::Tilemap;
  if (endsWith(n, ".anim.json")) return AssetKind::Animation;
  if (endsWith(n, ".font.json")) return AssetKind::Font;
  if (endsWith(n, ".json")) return AssetKind::Data;
  return AssetKind::Other;
}

std::string AssetDatabase::newGuid() {
  static std::mt19937_64 rng{std::random_device{}()};
  uint64_t a = rng(), b = rng();
  a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x4000ull;          // version 4
  b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;  // variant 10xx
  char buf[40];
  std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(a >> 32), static_cast<unsigned>((a >> 16) & 0xFFFF),
                static_cast<unsigned>(a & 0xFFFF), static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
  return buf;
}

bool AssetDatabase::validName(const std::string& n) {
  if (n.empty() || n == "." || n == ".." || n[0] == '.') return false;
  return n.find_first_of("/\\:*?\"<>|") == std::string::npos;
}

static bool goodGuid(const std::string& g) {
  if (g.size() != 36) return false;
  for (size_t i = 0; i < g.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (g[i] != '-') return false; }
    else if (!std::isxdigit(static_cast<unsigned char>(g[i]))) return false;
  }
  return true;
}

static std::string metaSettingsKey(const json& meta) {
  json k = json::object();
  if (meta.contains("importSettings")) k["importSettings"] = meta["importSettings"];
  if (meta.contains("targetOverrides")) k["targetOverrides"] = meta["targetOverrides"];
  return k.dump();
}

static bool readText(const fs::path& p, std::string& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

static int64_t mtimeOf(const fs::path& p) {
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec);
  return ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
}

std::string AssetDatabase::relOf(const fs::path& abs) const {
  std::error_code ec;
  fs::path r = fs::relative(abs, root_, ec);
  return ec ? abs.generic_string() : r.generic_string();
}

void AssetDatabase::open(const fs::path& projectRoot) {
  root_ = projectRoot;
  entries_.clear();
  byGuid_.clear();
  byRel_.clear();
  known_.clear();
  loadIndex();
}

void AssetDatabase::loadIndex() {
  std::string txt;
  if (!readText(root_ / "Library" / "AssetDatabase" / "index.json", txt)) return;
  json j = json::parse(txt, nullptr, false);
  if (j.is_discarded() || !j.contains("assets") || !j["assets"].is_object()) return;
  for (auto it = j["assets"].begin(); it != j["assets"].end(); ++it) {
    Known k;
    k.guid = it.value().value("guid", "");
    k.size = it.value().value("size", 0ull);
    k.mtime = it.value().value("mtime", static_cast<int64_t>(0));
    k.hash = std::stoull(it.value().value("hash", "0"), nullptr, 16);
    k.metaHash = std::stoull(it.value().value("metaHash", "0"), nullptr, 16);
    known_[it.key()] = k;
  }
}

void AssetDatabase::saveIndex() const {
  json j = json::object();
  j["assets"] = json::object();
  char b[2][32];
  for (const auto& kv : known_) {
    std::snprintf(b[0], sizeof b[0], "%llx", static_cast<unsigned long long>(kv.second.hash));
    std::snprintf(b[1], sizeof b[1], "%llx", static_cast<unsigned long long>(kv.second.metaHash));
    j["assets"][kv.first] = {{"guid", kv.second.guid}, {"size", kv.second.size}, {"mtime", kv.second.mtime}, {"hash", b[0]}, {"metaHash", b[1]}};
  }
  std::error_code ec;
  fs::create_directories(root_ / "Library" / "AssetDatabase", ec);
  const std::string text = j.dump(1) + "\n";
  std::string old;
  if (readText(root_ / "Library" / "AssetDatabase" / "index.json", old) && old == text) return;
  std::ofstream(root_ / "Library" / "AssetDatabase" / "index.json", std::ios::binary) << text;
}

bool AssetDatabase::writeMeta(AssetEntry& e) {
  const fs::path mp = fs::path(e.path.string() + ".meta");
  std::ofstream out(mp, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << e.meta.dump(2) << "\n";
  out.close();
  if (!out) return false;
  auto it = known_.find(e.rel);
  if (it != known_.end()) { it->second.metaHash = hashText(metaSettingsKey(e.meta)); saveIndex(); }  // own edits are not changes next time
  return true;
}

ScanReport AssetDatabase::scan() {
  ScanReport rep;
  struct File { fs::path abs; std::string rel; uint64_t size; int64_t mtime; };
  std::vector<File> files;
  std::set<std::string> metaRels;
  const fs::path assets = root_ / "Assets";
  std::error_code ec;
  for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (!name.empty() && name[0] == '.') {
      if (it->is_directory(ec)) it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    if (endsWith(name, ".meta")) { metaRels.insert(relOf(it->path())); continue; }
    File f;
    f.abs = it->path();
    f.rel = relOf(f.abs);
    f.size = it->file_size(ec);
    f.mtime = mtimeOf(f.abs);
    files.push_back(std::move(f));
  }
  std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.rel < b.rel; });

  std::set<std::string> fileRels;
  for (const File& f : files) fileRels.insert(f.rel);
  // Orphan .meta: its asset is gone. It may have moved (below).
  std::vector<std::string> orphans;  // asset rel
  for (const std::string& m : metaRels) {
    const std::string asset = m.substr(0, m.size() - 5);
    if (!fileRels.count(asset)) orphans.push_back(asset);
  }

  std::vector<AssetEntry> next;
  std::map<std::string, Known> nextKnown;
  std::set<std::string> seenGuids;
  std::set<std::string> usedOrphans;
  for (const File& f : files) {
    AssetEntry e;
    e.path = f.abs;
    e.rel = f.rel;
    e.kind = assetKindOf(f.abs);
    e.size = f.size;
    const fs::path mp(f.abs.string() + ".meta");
    bool newMeta = false, movedHere = false;
    Known k;
    auto kn = known_.find(f.rel);
    const bool hadKnown = kn != known_.end();
    if (hadKnown) k = kn->second;
    if (!metaRels.count(f.rel + ".meta")) {
      // New file. A moved file: an orphan .meta whose last known size and content hash match.
      const uint64_t h = hashFileContent(f.abs);
      for (const std::string& o : orphans) {
        if (usedOrphans.count(o)) continue;
        auto ok = known_.find(o);
        if (ok == known_.end() || ok->second.size != f.size || ok->second.hash != h || ok->second.guid.empty()) continue;
        std::error_code mec;
        fs::rename(root_ / (o + ".meta"), mp, mec);
        if (mec) continue;
        usedOrphans.insert(o);
        rep.moved.emplace_back(o, f.rel);
        k = ok->second;
        movedHere = true;
        break;
      }
      if (!movedHere) {
        e.meta = json::object();
        e.meta["guid"] = newGuid();
        e.meta["importer"] = assetKindName(e.kind);
        e.meta["importSettings"] = json::object();
        std::ofstream(mp, std::ios::binary) << e.meta.dump(2) << "\n";
        rep.created.push_back(f.rel);
        newMeta = true;
        k = Known();
        k.guid = e.meta["guid"].get<std::string>();
        k.hash = h;
        k.size = f.size;
        k.mtime = f.mtime;
        k.metaHash = hashText(metaSettingsKey(e.meta));
      }
    }
    if (!newMeta) {
      std::string txt;
      readText(mp, txt);
      e.meta = json::parse(txt, nullptr, false);
      if (e.meta.is_discarded() || !e.meta.is_object() || !e.meta.contains("guid") || !e.meta["guid"].is_string() ||
          !goodGuid(e.meta["guid"].get<std::string>())) {
        rep.errors.push_back(f.rel + ".meta: unreadable or without a valid guid");
        continue;
      }
      std::string g = e.meta["guid"].get<std::string>();
      if (seenGuids.count(g)) {  // a copied file with its .meta: the later one gets a new GUID
        g = newGuid();
        e.meta["guid"] = g;
        std::ofstream(mp, std::ios::binary) << e.meta.dump(2) << "\n";
        rep.regenerated.push_back(f.rel);
      }
      const uint64_t mh = hashText(metaSettingsKey(e.meta));
      if (hadKnown || movedHere) {
        bool changed = false;
        if (k.size != f.size || k.mtime != f.mtime) {
          const uint64_t h = hashFileContent(f.abs);
          changed = h != k.hash;
          k.hash = h;
        }
        if (k.metaHash != mh) changed = true;
        if (changed) rep.changed.push_back(f.rel);
      } else {
        k.hash = hashFileContent(f.abs);  // first sight of an asset that already has a .meta: no change reported
      }
      k.guid = e.meta["guid"].get<std::string>();
      k.size = f.size;
      k.mtime = f.mtime;
      k.metaHash = mh;
    }
    e.guid = e.meta["guid"].get<std::string>();
    seenGuids.insert(e.guid);
    nextKnown[f.rel] = k;
    next.push_back(std::move(e));
  }
  for (const std::string& o : orphans) {
    if (usedOrphans.count(o)) continue;
    std::error_code rec;
    fs::remove(root_ / (o + ".meta"), rec);
    rep.removed.push_back(o);
  }
  entries_ = std::move(next);
  byGuid_.clear();
  byRel_.clear();
  for (size_t i = 0; i < entries_.size(); ++i) { byGuid_[entries_[i].guid] = i; byRel_[entries_[i].rel] = i; }
  const bool indexChanged = nextKnown.size() != known_.size() || [&] {
    for (const auto& kv : nextKnown) {
      auto it = known_.find(kv.first);
      if (it == known_.end() || it->second.hash != kv.second.hash || it->second.mtime != kv.second.mtime || it->second.metaHash != kv.second.metaHash ||
          it->second.guid != kv.second.guid)
        return true;
    }
    return false;
  }();
  known_ = std::move(nextKnown);
  if (indexChanged || indexDirty_) saveIndex();
  indexDirty_ = false;
  return rep;
}

const AssetEntry* AssetDatabase::findGuid(const std::string& guid) const {
  auto it = byGuid_.find(guid);
  return it == byGuid_.end() ? nullptr : &entries_[it->second];
}

const AssetEntry* AssetDatabase::findPath(const fs::path& p) const {
  std::string rel = p.is_absolute() ? relOf(p) : p.generic_string();
  auto it = byRel_.find(rel);
  return it == byRel_.end() ? nullptr : &entries_[it->second];
}

std::string AssetDatabase::describeGuid(const std::string& guid) const {
  const AssetEntry* e = findGuid(guid);
  return e ? e->rel : "Missing (" + guid + ")";
}

json AssetDatabase::importSettings(const std::string& guid, const std::string& target) const {
  const AssetEntry* e = findGuid(guid);
  if (!e) return json::object();
  if (target.empty()) return e->meta.contains("importSettings") && e->meta["importSettings"].is_object() ? e->meta["importSettings"] : json::object();
  if (e->meta.contains("targetOverrides") && e->meta["targetOverrides"].is_object() && e->meta["targetOverrides"].contains(target) &&
      e->meta["targetOverrides"][target].is_object())
    return e->meta["targetOverrides"][target];
  return json::object();
}

bool AssetDatabase::setImportSettings(const std::string& guid, const std::string& target, const json& settings) {
  auto it = byGuid_.find(guid);
  if (it == byGuid_.end() || !settings.is_object()) return false;
  AssetEntry& e = entries_[it->second];
  json meta = e.meta;
  if (target.empty()) {
    meta["importSettings"] = settings;
  } else {
    if (!meta.contains("targetOverrides") || !meta["targetOverrides"].is_object()) meta["targetOverrides"] = json::object();
    if (settings.empty()) meta["targetOverrides"].erase(target);
    else meta["targetOverrides"][target] = settings;
    if (meta["targetOverrides"].empty()) meta.erase("targetOverrides");
  }
  if (meta == e.meta) return true;  // nothing to write
  e.meta = std::move(meta);
  return writeMeta(e);
}

std::string AssetDatabase::effectiveSettings(const std::string& guid, const std::string& target) const {
  const AssetEntry* e = findGuid(guid);
  return e ? effectiveImportSettings(e->meta, target) : std::string();
}

// ---- file operations ----
bool AssetDatabase::createFolder(const fs::path& parent, const std::string& name, std::string& err) {
  if (!validName(name)) { err = "'" + name + "' is not a valid folder name"; return false; }
  std::error_code ec;
  const fs::path p = parent / name;
  if (fs::exists(p, ec)) { err = "'" + name + "' already exists"; return false; }
  fs::create_directory(p, ec);
  if (ec) { err = ec.message(); return false; }
  return true;
}

static bool insideAssets(const fs::path& root, const fs::path& p) {
  std::error_code ec;
  const fs::path rel = fs::weakly_canonical(p, ec).lexically_relative(fs::weakly_canonical(root / "Assets", ec));
  return !rel.empty() && *rel.begin() != "..";
}

void AssetDatabase::rekey(const std::string& oldRel, const std::string& newRel) {
  std::map<std::string, Known> out;
  for (auto& kv : known_) {
    if (kv.first == oldRel) out[newRel] = kv.second;
    else if (kv.first.compare(0, oldRel.size() + 1, oldRel + "/") == 0) out[newRel + kv.first.substr(oldRel.size())] = kv.second;
    else out[kv.first] = kv.second;
  }
  known_ = std::move(out);
  indexDirty_ = true;
}

bool AssetDatabase::moveImpl(const fs::path& from, const fs::path& to, std::string& err) {
  std::error_code ec;
  if (!insideAssets(root_, from) || !insideAssets(root_, to)) { err = "only paths inside Assets/ can be moved"; return false; }
  if (!fs::exists(from, ec)) { err = "'" + from.filename().string() + "' does not exist"; return false; }
  if (fs::exists(to, ec)) { err = "'" + to.filename().string() + "' already exists"; return false; }
  const bool dir = fs::is_directory(from, ec);
  if (dir && relOf(to).compare(0, relOf(from).size() + 1, relOf(from) + "/") == 0) { err = "cannot move a folder into itself"; return false; }
  const std::string oldRel = relOf(from), newRel = relOf(to);
  fs::rename(from, to, ec);
  if (ec) { err = ec.message(); return false; }
  if (!dir) {
    const fs::path m(from.string() + ".meta");
    if (fs::exists(m, ec)) fs::rename(m, fs::path(to.string() + ".meta"), ec);
  }
  rekey(oldRel, newRel);
  scan();
  return true;
}

bool AssetDatabase::rename(const fs::path& from, const std::string& newName, std::string& err) {
  if (!validName(newName)) { err = "'" + newName + "' is not a valid name"; return false; }
  return moveImpl(from, from.parent_path() / newName, err);
}

bool AssetDatabase::move(const fs::path& from, const fs::path& toDir, std::string& err) {
  return moveImpl(from, toDir / from.filename(), err);
}

bool AssetDatabase::remove(const fs::path& p, std::string& err) {
  std::error_code ec;
  if (!insideAssets(root_, p) || relOf(p) == "Assets") { err = "only paths inside Assets/ can be deleted"; return false; }
  if (!fs::exists(p, ec)) { err = "does not exist"; return false; }
  const std::string rel = relOf(p);
  if (fs::is_directory(p, ec)) fs::remove_all(p, ec);
  else { fs::remove(p, ec); fs::remove(fs::path(p.string() + ".meta"), ec); }
  if (ec) { err = ec.message(); return false; }
  indexDirty_ = true;
  for (auto it = known_.begin(); it != known_.end();)
    it = (it->first == rel || it->first.compare(0, rel.size() + 1, rel + "/") == 0) ? known_.erase(it) : std::next(it);
  scan();
  return true;
}

}  // namespace editor
}  // namespace rensga
