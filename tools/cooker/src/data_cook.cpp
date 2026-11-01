#include "rensga/cooker/data_cook.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "rensga/DataAsset.h"
#include "rensga/core/reflect_json.h"

namespace rensga {
namespace cooker {

using nlohmann::json;

bool isDataAssetType(const TypeInfo& t) {
  return t.name && !t.asComponent && !t.asBehaviour && t.construct && t.fieldCount > 0 && t.size <= kDataAssetMaxBytes && blobSize(t) <= kDataAssetMaxBytes;
}

std::vector<const TypeInfo*> dataAssetTypes() {
  std::vector<const TypeInfo*> out;
  const TypeRegistry& reg = TypeRegistry::instance();
  for (u32 i = 0; i < reg.count(); ++i)
    if (const TypeInfo* t = reg.byId(i))
      if (isDataAssetType(*t)) out.push_back(t);
  std::sort(out.begin(), out.end(), [](const TypeInfo* a, const TypeInfo* b) { return std::string(a->name) < std::string(b->name); });
  return out;
}

bool parseDataAsset(const json& doc, const TypeInfo*& type, json& fields, std::string& err) {
  type = nullptr;
  fields = json::object();
  if (!doc.is_object()) { err = "not a JSON object"; return false; }
  if (!doc.contains("type") || !doc["type"].is_string() || doc["type"].get<std::string>().empty()) {
    err = "no \"type\": open it in the Inspector and pick a data asset type, or create it with Create > Data Asset > <Type>";
    return false;
  }
  const std::string name = doc["type"].get<std::string>();
  const TypeInfo* t = TypeRegistry::instance().find(name.c_str());
  if (!t) { err = "type '" + name + "' is not registered (is the script compiled? it must be a REFLECT struct in Assets/Scripts)"; return false; }
  if (t->asComponent || t->asBehaviour) { err = "type '" + name + "' is a component, not a data asset (derive it from DataAsset, not Behaviour)"; return false; }
  if (!isDataAssetType(*t)) { err = "type '" + name + "' cannot be a data asset (no reflected fields, or more than " + std::to_string(kDataAssetMaxBytes) + " bytes)"; return false; }
  if (doc.contains("fields")) {
    if (!doc["fields"].is_object()) { err = "\"fields\" must be an object"; return false; }
    fields = doc["fields"];
  }
  type = t;
  return true;
}

void cookDataAssets(const std::vector<DataAssetRef>& assetsIn, const std::function<u32(const std::string&)>& addStr, std::vector<u8>& blob,
                    std::vector<PackDataAsset>& out, std::vector<DataIssue>& issues) {
  std::vector<DataAssetRef> assets = assetsIn;
  std::sort(assets.begin(), assets.end(), [](const DataAssetRef& a, const DataAssetRef& b) { return a.rel < b.rel; });
  for (const DataAssetRef& a : assets) {
    std::ifstream f(a.path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const json doc = json::parse(ss.str(), nullptr, false);
    if (doc.is_discarded()) { issues.push_back({"data asset is not valid JSON", a.rel, a.guid}); continue; }
    const TypeInfo* t = nullptr;
    json fields;
    std::string err;
    if (!parseDataAsset(doc, t, fields, err)) { issues.push_back({"data asset: " + err, a.rel, a.guid}); continue; }
    PackDataAsset rec = {};
    if (!Guid::parse(a.guid.c_str(), rec.guid)) { issues.push_back({"data asset has no valid GUID", a.rel, a.guid}); continue; }
    alignas(16) u8 obj[kDataAssetMaxBytes], defaults[kDataAssetMaxBytes];
    t->construct(obj);
    t->construct(defaults);
    std::string ferr;
    if (!fieldsFromJson(*t, obj, fields, &ferr)) {
      issues.push_back({"data asset of type '" + std::string(t->name) + "': " + ferr, a.rel, a.guid});
      continue;
    }
    // Unknown names in "fields" are mistakes (a renamed or removed field): say so instead of silently using the default.
    bool bad = false;
    for (auto it = fields.begin(); it != fields.end() && !bad; ++it) {
      bool known = false;
      for (u32 i = 0; i < t->fieldCount; ++i) known = known || it.key() == t->fields[i].name;
      if (!known) {
        issues.push_back({"data asset of type '" + std::string(t->name) + "' has no field '" + it.key() + "' (renamed or removed in the script? edit any value of the asset in the Inspector to drop it)", a.rel, a.guid});
        bad = true;
      }
    }
    if (bad) continue;
    std::vector<u8> bytes(blobSize(*t));
    writeBlob(*t, obj, bytes.data());
    u32 size = trimmedBlobSize(*t, obj, defaults);
    if (size > bytes.size()) size = static_cast<u32>(bytes.size());
    while (blob.size() % 16) blob.push_back(0);
    rec.typeHash = t->hash;
    std::string name = a.rel;
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    const std::string suffix = ".asset.json";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) name.resize(name.size() - suffix.size());
    rec.nameStr = addStr(name);
    rec.blobOffset = static_cast<u32>(blob.size());
    rec.blobSize = size;
    blob.insert(blob.end(), bytes.begin(), bytes.begin() + size);
    out.push_back(rec);
  }
}

}  // namespace cooker
}  // namespace rensga
