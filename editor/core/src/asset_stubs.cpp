#include "rensga/editor/asset_stubs.h"

#include <fstream>

#include "rensga/editor/prefab_format.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
const StubKind kStubs[] = {
    {AssetKind::Prefab, "Prefab", ".prefab", "New Prefab", "PFB"},
    {AssetKind::Material, "Material", ".mat.json", "New Material", "MAT"},
    {AssetKind::AnimatorController, "Animator Controller", ".controller.json", "New Animator Controller", "CTL"},
    {AssetKind::PropertyClip, "Property Clip", ".clip.json", "New Property Clip", "CLP"},
    {AssetKind::DataAsset, "Data Asset", ".asset.json", "New Data Asset", "DAT"},
};
}  // namespace

const StubKind* stubKinds(int* count) {
  if (count) *count = static_cast<int>(sizeof kStubs / sizeof kStubs[0]);
  return kStubs;
}

const StubKind* stubKindFor(AssetKind k) {
  for (const StubKind& s : kStubs) if (s.kind == k) return &s;
  return nullptr;
}

std::string stubText(AssetKind k, const std::string& displayName, const std::string& guid) {
  json j = json::object();
  switch (k) {
    case AssetKind::Prefab: {
      PrefabAsset p;
      p.guid = guid;
      p.rootFileId = p.allocFileId();
      PrefabObject root;
      root.fileId = p.rootFileId;
      root.name = displayName;
      p.objects.push_back(root);
      return toJson(p).dump(1) + "\n";
    }
    case AssetKind::Material:  // pack-format.md "Material asset"
      j = json::parse(R"({"format":1,"texture":"","color":[255,255,255,255],"unlit":false,"alphaTest":false})");
      break;
    case AssetKind::AnimatorController: j = json{{"format", 1}, {"kind", "AnimatorController"}}; break;
    case AssetKind::PropertyClip: j = json{{"format", 1}, {"kind", "PropertyClip"}}; break;
    case AssetKind::DataAsset: j = json{{"format", 1}, {"kind", "DataAsset"}}; break;
    default: break;
  }
  return j.dump(1) + "\n";
}

bool createAssetStub(AssetDatabase& db, AssetKind k, const fs::path& folder, const std::string& name, fs::path& created, std::string& err) {
  const StubKind* sk = stubKindFor(k);
  if (!sk) { err = "this kind is not created from a stub"; return false; }
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) { err = "not a folder: " + folder.string(); return false; }
  std::string base = name.empty() ? sk->baseName : name;
  const std::string ext = sk->extension;
  if (base.size() > ext.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0) base.resize(base.size() - ext.size());
  if (base.find_first_of("/\\") != std::string::npos || base == "." || base == "..") { err = "invalid name"; return false; }
  fs::path p = folder / (base + ext);
  for (int i = 1; fs::exists(p, ec) && i < 1000; ++i) p = folder / (base + " " + std::to_string(i) + ext);
  if (fs::exists(p, ec)) { err = "no free name for " + base; return false; }
  {
    std::ofstream f(p, std::ios::binary);
    if (!f) { err = "cannot write " + p.string(); return false; }
    f << stubText(k, p.filename().string().substr(0, p.filename().string().size() - ext.size()), std::string());
  }
  db.scan();
  const AssetEntry* e = db.findPath(p);
  if (!e) { err = "the new file was not imported"; return false; }
  if (k == AssetKind::Prefab) {
    PrefabAsset pa;
    std::string e2;
    if (loadPrefab(p.string(), pa, e2)) { pa.guid = e->guid; savePrefab(p.string(), pa, e2); db.scan(); }
  }
  created = p;
  return true;
}

}  // namespace editor
}  // namespace rensga
