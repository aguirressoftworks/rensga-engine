// F3.02 (Editor 3): the asset database. Every file under Assets/ has a sidecar `<file>.meta` (JSON):
//
//   { "guid": "<36 chars>", "importer": "texture", "importSettings": {...},
//     "targetOverrides": { "<build target id>": {...} } }
//
// "importer" is optional in files written before F3.02 (it is derived from the extension and written only to new files).
// Existing .meta files are never rewritten unless their settings are edited. Folders have no .meta.
// References between assets are by GUID, so renaming or moving an asset (here, or outside the editor) never breaks them.
//
// Build target ids are the ids of the cooker's format plugins: the project's console target and the platform builds.
// editor/core never names one.
//
// Drag and drop: the Project window starts an ImGui drag with the payload type `RENSGA_ASSET_GUID`. The payload is the
// asset's 36-character GUID followed by a NUL byte (37 bytes). Drop targets look the GUID up with AssetDatabase::findGuid.
//
// Missing assets: a GUID no asset carries is shown as "Missing (<guid>)" (describeGuid).
// No ImGui here. Main thread only (the FileWatcher is the one piece with a thread).
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

constexpr const char* kAssetGuidPayload = "RENSGA_ASSET_GUID";

enum class AssetKind { Folder, Texture, Audio, Model, Scene, Script, Primitive, Tilemap, Animation, Font, Data, Other };
const char* assetKindName(AssetKind k);  // "texture", "audio", ... (also the "importer" value written to new .meta files)
AssetKind assetKindOf(const std::filesystem::path& file);

struct AssetEntry {
  std::string guid;
  std::filesystem::path path;  // absolute
  std::string rel;             // project-relative, '/' separators ("Assets/Textures/player.png")
  AssetKind kind = AssetKind::Other;
  uint64_t size = 0;
  nlohmann::json meta = nlohmann::json::object();  // parsed .meta
};

struct ScanReport {
  std::vector<std::string> created;      // rel paths that got a new .meta
  std::vector<std::pair<std::string, std::string>> moved;  // external moves detected by content hash: from -> to
  std::vector<std::string> removed;      // rel paths whose asset disappeared (orphan .meta deleted)
  std::vector<std::string> changed;      // rel paths whose content or import settings changed since the last scan
  std::vector<std::string> regenerated;  // rel paths that got a new GUID because another asset carried the same one
  std::vector<std::string> errors;       // unreadable .meta files
  bool any() const { return !(created.empty() && moved.empty() && removed.empty() && changed.empty() && regenerated.empty()); }
};

class AssetDatabase {
 public:
  // Sets the project root and loads Library/AssetDatabase/index.json (sizes, times and content hashes of the last scan).
  void open(const std::filesystem::path& projectRoot);
  const std::filesystem::path& root() const { return root_; }

  // Walks Assets/: creates missing .meta files, follows external moves, drops orphan .meta files, reports changes.
  ScanReport scan();

  const std::vector<AssetEntry>& entries() const { return entries_; }  // sorted by rel
  const AssetEntry* findGuid(const std::string& guid) const;
  const AssetEntry* findPath(const std::filesystem::path& p) const;     // absolute, or relative to the project root
  // Path of the asset ("Assets/..."), or "Missing (<guid>)".
  std::string describeGuid(const std::string& guid) const;

  // ---- import settings ----
  // target "" = the default settings, else the override for that build target id.
  nlohmann::json importSettings(const std::string& guid, const std::string& target) const;
  // Replaces the settings (or the override) and rewrites the .meta. An empty object removes the override. Returns false
  // for an unknown GUID or a write error. The next scan() does not report this change.
  bool setImportSettings(const std::string& guid, const std::string& target, const nlohmann::json& settings);
  // Settings the cook uses for `target` (override laid over the defaults), as cooker text.
  std::string effectiveSettings(const std::string& guid, const std::string& target) const;

  // ---- file operations (the .meta moves with the file) ----
  bool createFolder(const std::filesystem::path& parent, const std::string& name, std::string& err);
  bool rename(const std::filesystem::path& from, const std::string& newName, std::string& err);  // file or folder
  bool move(const std::filesystem::path& from, const std::filesystem::path& toDir, std::string& err);
  bool remove(const std::filesystem::path& p, std::string& err);

  static std::string newGuid();  // random v4
  static bool validName(const std::string& name);

 private:
  struct Known { std::string guid; uint64_t size = 0; int64_t mtime = 0; uint64_t hash = 0; uint64_t metaHash = 0; };
  void loadIndex();
  void saveIndex() const;
  bool writeMeta(AssetEntry& e);
  std::string relOf(const std::filesystem::path& abs) const;
  void rekey(const std::string& oldRel, const std::string& newRel);
  bool moveImpl(const std::filesystem::path& from, const std::filesystem::path& to, std::string& err);

  std::filesystem::path root_;
  std::vector<AssetEntry> entries_;
  std::map<std::string, size_t> byGuid_, byRel_;
  std::map<std::string, Known> known_;  // by rel
  bool indexDirty_ = false;
};

uint64_t hashFileContent(const std::filesystem::path& p);  // FNV-1a 64 over the bytes
uint64_t hashText(const std::string& s);

}  // namespace editor
}  // namespace rensga
