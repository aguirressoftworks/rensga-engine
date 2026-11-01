#include "rensga/editor/data_asset_doc.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include "rensga/cooker/data_cook.h"
#include "rensga/core/reflect_json.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

std::vector<std::string> dataAssetTypeNames() {
  std::vector<std::string> out;
  for (const TypeInfo* t : cooker::dataAssetTypes()) out.push_back(t->name);
  return out;
}

namespace {
bool writeAtomic(const fs::path& p, const std::string& text, std::string& err) {
  const fs::path tmp = p.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary);
    if (!f) { err = "cannot write " + tmp.string(); return false; }
    f << text;
    if (!f) { err = "cannot write " + tmp.string(); return false; }
  }
  std::error_code ec;
  fs::rename(tmp, p, ec);
  if (ec) { err = "cannot replace " + p.string() + ": " + ec.message(); return false; }
  return true;
}
ordered_json defaultsOf(const TypeInfo& t) {
  alignas(16) unsigned char obj[512];
  t.construct(obj);
  ordered_json j = fieldsToOrderedJson(t, obj);
  if (t.destruct) t.destruct(obj);
  return j;
}
}  // namespace

bool createDataAsset(AssetDatabase& db, const fs::path& folder, const std::string& typeName, const std::string& nameIn, fs::path& created, std::string& err) {
  const TypeInfo* t = TypeRegistry::instance().find(typeName.c_str());
  if (!t || !cooker::isDataAssetType(*t)) { err = "'" + typeName + "' is not a data asset type (a reflected struct that derives from DataAsset)"; return false; }
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) { err = "not a folder: " + folder.string(); return false; }
  std::string base = nameIn.empty() ? "New " + typeName : nameIn;
  const std::string ext = ".asset.json";
  if (base.size() > ext.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0) base.resize(base.size() - ext.size());
  if (base.find_first_of("/\\") != std::string::npos || base == "." || base == "..") { err = "invalid name"; return false; }
  fs::path p = folder / (base + ext);
  for (int i = 1; fs::exists(p, ec) && i < 1000; ++i) p = folder / (base + " " + std::to_string(i) + ext);
  if (fs::exists(p, ec)) { err = "no free name for " + base; return false; }
  ordered_json j = ordered_json::object();
  j["format"] = 1;
  j["kind"] = "DataAsset";
  j["type"] = typeName;
  j["fields"] = defaultsOf(*t);
  if (!writeAtomic(p, j.dump(1) + "\n", err)) return false;
  db.scan();
  if (!db.findPath(p)) { err = "the new file was not imported"; return false; }
  created = p;
  return true;
}

const TypeInfo* DataAssetDoc::type() const {
  const TypeInfo* t = TypeRegistry::instance().find(type_.c_str());
  return t && cooker::isDataAssetType(*t) ? t : nullptr;
}

bool DataAssetDoc::load(const fs::path& file, std::string& err) {
  std::ifstream f(file, std::ios::binary);
  if (!f) { err = "cannot read " + file.string(); return false; }
  std::stringstream ss;
  ss << f.rdbuf();
  ordered_json j = ordered_json::parse(ss.str(), nullptr, false);
  if (j.is_discarded() || !j.is_object()) { err = "not valid JSON: " + file.string(); return false; }
  path_ = file;
  raw_ = j;
  type_ = j.contains("type") && j["type"].is_string() ? j["type"].get<std::string>() : std::string();
  if (!raw_.contains("fields") || !raw_["fields"].is_object()) raw_["fields"] = ordered_json::object();
  undo_.clear();
  redo_.clear();
  gesture_ = 0;
  gesturePushed_ = false;
  return true;
}

json DataAssetDoc::values() const {
  const TypeInfo* t = type();
  json out = json::object();
  if (!t) return out;
  alignas(16) unsigned char obj[512];
  t->construct(obj);
  fieldsFromJson(*t, obj, raw_["fields"], nullptr);
  out = fieldsToJson(*t, obj);
  if (t->destruct) t->destruct(obj);
  return out;
}

json DataAssetDoc::value(const std::string& field) const {
  const json v = values();
  return v.contains(field) ? v[field] : json();
}

std::vector<std::string> DataAssetDoc::unknownFields() const {
  std::vector<std::string> out;
  const TypeInfo* t = type();
  if (!t) return out;
  for (auto it = raw_["fields"].begin(); it != raw_["fields"].end(); ++it) {
    bool known = false;
    for (u32 i = 0; i < t->fieldCount; ++i) known = known || it.key() == t->fields[i].name;
    if (!known) out.push_back(it.key());
  }
  return out;
}

void DataAssetDoc::push() {
  undo_.push_back(raw_["fields"]);
  if (undo_.size() > 200) undo_.erase(undo_.begin());
  redo_.clear();
}

bool DataAssetDoc::set(const std::string& field, const json& value, std::string& err) {
  const TypeInfo* t = type();
  if (!t) { err = "the type '" + type_ + "' is not loaded (script missing or still compiling)"; return false; }
  bool known = false;
  for (u32 i = 0; i < t->fieldCount; ++i) known = known || field == t->fields[i].name;
  if (!known) { err = "'" + type_ + "' has no field '" + field + "'"; return false; }
  // Validate on a scratch instance: the value must fit the reflected field.
  alignas(16) unsigned char obj[512];
  t->construct(obj);
  json one = json::object();
  one[field] = value;
  std::string ferr;
  const bool ok = fieldsFromJson(*t, obj, one, &ferr);
  if (!ok) { if (t->destruct) t->destruct(obj); err = ferr.empty() ? "the value does not fit the field" : ferr; return false; }
  // The new document keeps the other values and drops fields the type no longer has.
  fieldsFromJson(*t, obj, raw_["fields"], nullptr);
  fieldsFromJson(*t, obj, one, nullptr);
  const ordered_json next = fieldsToOrderedJson(*t, obj);
  if (t->destruct) t->destruct(obj);
  if (next == raw_["fields"]) return true;  // nothing changed
  if (gesture_ == 0 || !gesturePushed_) { push(); gesturePushed_ = gesture_ > 0; }
  raw_["fields"] = next;
  return save(err);
}

void DataAssetDoc::beginGesture() {
  if (gesture_++ == 0) gesturePushed_ = false;
}
void DataAssetDoc::endGesture() {
  if (gesture_ > 0) --gesture_;
  if (gesture_ == 0) gesturePushed_ = false;
}

bool DataAssetDoc::undo() {
  if (undo_.empty()) return false;
  redo_.push_back(raw_["fields"]);
  raw_["fields"] = undo_.back();
  undo_.pop_back();
  std::string err;
  return save(err);
}
bool DataAssetDoc::redo() {
  if (redo_.empty()) return false;
  undo_.push_back(raw_["fields"]);
  raw_["fields"] = redo_.back();
  redo_.pop_back();
  std::string err;
  return save(err);
}

bool DataAssetDoc::save(std::string& err) {
  if (path_.empty()) { err = "no file"; return false; }
  return writeAtomic(path_, raw_.dump(1) + "\n", err);
}

}  // namespace editor
}  // namespace rensga
