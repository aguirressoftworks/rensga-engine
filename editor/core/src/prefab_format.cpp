#include "rensga/editor/prefab_format.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace rensga {
namespace editor {

using nlohmann::json;
using nlohmann::ordered_json;

namespace {

bool allDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

ordered_json oj(const json& j) { return ordered_json::parse(j.dump()); }

ordered_json arrN(const float* v, int n) {
  ordered_json a = ordered_json::array();
  for (int i = 0; i < n; ++i) a.push_back(v[i]);
  return a;
}

bool readN(const json& j, float* v, int n) {
  if (!j.is_array() || static_cast<int>(j.size()) != n) return false;
  for (int i = 0; i < n; ++i) {
    if (!j[static_cast<size_t>(i)].is_number()) return false;
    v[i] = j[static_cast<size_t>(i)].get<float>();
  }
  return true;
}

ordered_json modsToJson(const PrefabModifications& m) {
  ordered_json j = ordered_json::object();
  if (!m.overrides.empty()) {
    ordered_json a = ordered_json::array();
    for (const PrefabOverride& o : m.overrides) {
      ordered_json e = ordered_json::object();
      e["fileId"] = fileIdToJson(o.fileId);
      e["path"] = o.path;
      if (o.occurrence != 0) e["occurrence"] = o.occurrence;
      e["value"] = oj(o.value);
      a.push_back(std::move(e));
    }
    j["overrides"] = std::move(a);
  }
  if (!m.addedComponents.empty()) {
    ordered_json a = ordered_json::array();
    for (const PrefabAddedComponent& c : m.addedComponents) {
      ordered_json e = ordered_json::object();
      e["fileId"] = fileIdToJson(c.fileId);
      e["componentId"] = fileIdToJson(c.component.fileId);
      e["type"] = c.component.type;
      e["fields"] = oj(c.component.fields);
      a.push_back(std::move(e));
    }
    j["addedComponents"] = std::move(a);
  }
  if (!m.removedComponents.empty()) {
    ordered_json a = ordered_json::array();
    for (const PrefabRemovedComponent& c : m.removedComponents) {
      ordered_json e = ordered_json::object();
      e["fileId"] = fileIdToJson(c.fileId);
      e["type"] = c.type;
      if (c.occurrence != 0) e["occurrence"] = c.occurrence;
      a.push_back(std::move(e));
    }
    j["removedComponents"] = std::move(a);
  }
  if (!m.removedObjects.empty()) {
    ordered_json a = ordered_json::array();
    for (const FileId& f : m.removedObjects) a.push_back(oj(fileIdToJson(f)));
    j["removedObjects"] = std::move(a);
  }
  if (m.addedObjects.is_array() && !m.addedObjects.empty()) j["addedObjects"] = oj(m.addedObjects);
  return j;
}

bool modsFromJson(const json& j, PrefabModifications& m, std::string& err) {
  m = PrefabModifications();
  if (j.contains("overrides")) {
    if (!j["overrides"].is_array()) { err = "overrides must be an array"; return false; }
    for (const json& e : j["overrides"]) {
      PrefabOverride o;
      o.fileId = e.is_object() ? fileIdFromJson(e.value("fileId", json())) : FileId();
      if (o.fileId.empty() || !e.contains("path") || !e["path"].is_string() || !e.contains("value")) { err = "an override needs fileId, path and value"; return false; }
      o.path = e["path"].get<std::string>();
      o.occurrence = e.value("occurrence", 0);
      o.value = e["value"];
      m.overrides.push_back(std::move(o));
    }
  }
  if (j.contains("addedComponents")) {
    if (!j["addedComponents"].is_array()) { err = "addedComponents must be an array"; return false; }
    for (const json& e : j["addedComponents"]) {
      PrefabAddedComponent c;
      c.fileId = e.is_object() ? fileIdFromJson(e.value("fileId", json())) : FileId();
      c.component.fileId = e.is_object() ? fileIdFromJson(e.value("componentId", json())) : FileId();
      if (c.fileId.empty() || !e.contains("type") || !e["type"].is_string()) { err = "an added component needs fileId and type"; return false; }
      c.component.type = e["type"].get<std::string>();
      c.component.fields = e.value("fields", json::object());
      m.addedComponents.push_back(std::move(c));
    }
  }
  if (j.contains("removedComponents")) {
    if (!j["removedComponents"].is_array()) { err = "removedComponents must be an array"; return false; }
    for (const json& e : j["removedComponents"]) {
      PrefabRemovedComponent c;
      c.fileId = e.is_object() ? fileIdFromJson(e.value("fileId", json())) : FileId();
      if (c.fileId.empty() || !e.contains("type") || !e["type"].is_string()) { err = "a removed component needs fileId and type"; return false; }
      c.type = e["type"].get<std::string>();
      c.occurrence = e.value("occurrence", 0);
      m.removedComponents.push_back(std::move(c));
    }
  }
  if (j.contains("removedObjects")) {
    if (!j["removedObjects"].is_array()) { err = "removedObjects must be an array"; return false; }
    for (const json& e : j["removedObjects"]) {
      FileId f = fileIdFromJson(e);
      if (f.empty()) { err = "removedObjects entries are file ids"; return false; }
      m.removedObjects.push_back(f);
    }
  }
  if (j.contains("addedObjects")) {
    if (!j["addedObjects"].is_array()) { err = "addedObjects must be an array"; return false; }
    m.addedObjects = j["addedObjects"];
  }
  return true;
}

bool validOverridePath(const std::string& p) {
  if (p.size() > 1 && p[0] == '@') {
    static const char* const kObj[] = {"@name", "@active", "@tag", "@layer", "@position", "@rotation", "@scale"};
    for (const char* k : kObj) if (p == k) return true;
    return false;
  }
  const size_t dot = p.find('.');
  return dot != std::string::npos && dot > 0 && dot + 1 < p.size();  // "Type.field[...]"; fields are checked against reflection by A1
}

}  // namespace

FileId fileIdFromJson(const json& j) {
  if (j.is_number_integer()) return std::to_string(j.get<long long>());
  if (j.is_string()) {
    const std::string s = j.get<std::string>();
    // digits separated by '/'
    bool ok = !s.empty();
    std::string part;
    for (size_t i = 0; i <= s.size() && ok; ++i) {
      if (i == s.size() || s[i] == '/') { ok = allDigits(part); part.clear(); }
      else part += s[i];
    }
    return ok ? s : FileId();
  }
  return FileId();
}

json fileIdToJson(const FileId& id) {
  if (allDigits(id) && id.size() < 18) return json(std::stoll(id));
  return json(id);
}

const PrefabObject* PrefabAsset::find(const FileId& id) const {
  for (const PrefabObject& o : objects) if (o.fileId == id) return &o;
  return nullptr;
}

void writeInstanceKeys(ordered_json& so, const PrefabInstance& inst) {
  ordered_json ref = ordered_json::object();
  ref["asset"] = inst.asset;
  ref["fileId"] = fileIdToJson(inst.fileId);
  so["prefab"] = std::move(ref);
  const ordered_json m = modsToJson(inst.mods);
  for (auto it = m.begin(); it != m.end(); ++it) so[it.key()] = it.value();
}

bool readInstanceKeys(const json& so, PrefabInstance& out, std::string& err) {
  if (!so.is_object() || !so.contains("prefab") || so["prefab"].is_null()) return false;
  const json& r = so["prefab"];
  if (!r.is_object() || !r.contains("asset") || !r["asset"].is_string()) { err = "prefab needs an asset GUID"; return false; }
  PrefabInstance i;
  i.asset = r["asset"].get<std::string>();
  i.fileId = fileIdFromJson(r.value("fileId", json()));
  if (i.fileId.empty()) { err = "prefab needs the root fileId"; return false; }
  if (!modsFromJson(so, i.mods, err)) return false;
  out = std::move(i);
  return true;
}

ordered_json toJson(const PrefabAsset& p) {
  ordered_json j = ordered_json::object();
  j["format"] = kPrefabFormat;
  j["guid"] = p.guid;
  if (p.isVariant()) {
    ordered_json v = ordered_json::object();
    v["base"] = p.variantBase;
    const ordered_json m = modsToJson(p.variant);
    for (auto it = m.begin(); it != m.end(); ++it) v[it.key()] = it.value();
    j["variant"] = std::move(v);
    j["nextFileId"] = p.nextFileId;
    return j;
  }
  j["rootFileId"] = fileIdToJson(p.rootFileId);
  j["nextFileId"] = p.nextFileId;
  ordered_json objs = ordered_json::array();
  for (const PrefabObject& o : p.objects) {
    ordered_json e = ordered_json::object();
    e["fileId"] = fileIdToJson(o.fileId);
    e["name"] = o.name;
    e["parent"] = o.parent.empty() ? ordered_json(nullptr) : ordered_json(fileIdToJson(o.parent));
    if (!o.active) e["active"] = false;
    if (!o.tag.empty()) e["tag"] = o.tag;
    if (o.layer != 0) e["layer"] = o.layer;
    ordered_json t = ordered_json::object();
    t["position"] = arrN(o.position, 3);
    t["rotation"] = arrN(o.rotation, 4);
    t["scale"] = arrN(o.scale, 3);
    e["transform"] = std::move(t);
    if (!o.nestedAsset.empty()) {
      ordered_json n = ordered_json::object();
      n["asset"] = o.nestedAsset;
      n["fileId"] = fileIdToJson(o.nestedRootFileId);
      const ordered_json m = modsToJson(o.nested);
      for (auto it = m.begin(); it != m.end(); ++it) n[it.key()] = it.value();
      e["prefab"] = std::move(n);
    }
    ordered_json comps = ordered_json::array();
    for (const PrefabComponent& c : o.components) {
      ordered_json cj = ordered_json::object();
      cj["fileId"] = fileIdToJson(c.fileId);
      cj["type"] = c.type;
      cj["fields"] = oj(c.fields);
      comps.push_back(std::move(cj));
    }
    e["components"] = std::move(comps);
    objs.push_back(std::move(e));
  }
  j["objects"] = std::move(objs);
  return j;
}

bool fromJson(const json& j, PrefabAsset& out, std::string& err) {
  if (!j.is_object()) { err = "not a JSON object"; return false; }
  if (!j.contains("format") || !j["format"].is_number_integer() || j["format"].get<int>() != kPrefabFormat) { err = "unsupported prefab format version"; return false; }
  PrefabAsset p;
  p.guid = j.value("guid", std::string());
  p.nextFileId = j.value("nextFileId", 1LL);
  if (j.contains("variant")) {
    const json& v = j["variant"];
    if (!v.is_object() || !v.contains("base") || !v["base"].is_string()) { err = "variant needs a base GUID"; return false; }
    if (j.contains("objects")) { err = "a variant has no objects of its own"; return false; }
    p.variantBase = v["base"].get<std::string>();
    if (!modsFromJson(v, p.variant, err)) return false;
    out = std::move(p);
    return true;
  }
  p.rootFileId = fileIdFromJson(j.value("rootFileId", json()));
  if (!j.contains("objects") || !j["objects"].is_array()) { err = "objects must be an array"; return false; }
  for (const json& e : j["objects"]) {
    PrefabObject o;
    if (!e.is_object()) { err = "an object entry is not an object"; return false; }
    o.fileId = fileIdFromJson(e.value("fileId", json()));
    if (o.fileId.empty()) { err = "an object has no fileId"; return false; }
    o.name = e.value("name", std::string());
    if (e.contains("parent") && !e["parent"].is_null()) {
      o.parent = fileIdFromJson(e["parent"]);
      if (o.parent.empty()) { err = "object " + o.fileId + ": bad parent"; return false; }
    }
    o.active = e.value("active", true);
    o.tag = e.value("tag", std::string());
    o.layer = e.value("layer", 0);
    if (e.contains("transform")) {
      const json& t = e["transform"];
      if (!t.is_object() || (t.contains("position") && !readN(t["position"], o.position, 3)) ||
          (t.contains("rotation") && !readN(t["rotation"], o.rotation, 4)) || (t.contains("scale") && !readN(t["scale"], o.scale, 3))) {
        err = "object " + o.fileId + ": bad transform";
        return false;
      }
    }
    if (e.contains("prefab") && !e["prefab"].is_null()) {
      const json& n = e["prefab"];
      if (!n.is_object() || !n.contains("asset") || !n["asset"].is_string()) { err = "object " + o.fileId + ": nested prefab needs an asset GUID"; return false; }
      o.nestedAsset = n["asset"].get<std::string>();
      o.nestedRootFileId = fileIdFromJson(n.value("fileId", json()));
      if (!modsFromJson(n, o.nested, err)) return false;
    }
    if (e.contains("components")) {
      if (!e["components"].is_array()) { err = "object " + o.fileId + ": components must be an array"; return false; }
      for (const json& c : e["components"]) {
        PrefabComponent pc;
        pc.fileId = c.is_object() ? fileIdFromJson(c.value("fileId", json())) : FileId();
        if (pc.fileId.empty() || !c.contains("type") || !c["type"].is_string()) { err = "object " + o.fileId + ": a component needs fileId and type"; return false; }
        pc.type = c["type"].get<std::string>();
        pc.fields = c.value("fields", json::object());
        o.components.push_back(std::move(pc));
      }
    }
    p.objects.push_back(std::move(o));
  }
  out = std::move(p);
  return true;
}

bool loadPrefab(const std::string& path, PrefabAsset& out, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { err = "cannot open " + path; return false; }
  std::ostringstream ss;
  ss << f.rdbuf();
  json j = json::parse(ss.str(), nullptr, false);
  if (j.is_discarded()) { err = path + ": invalid JSON"; return false; }
  std::string e;
  if (!fromJson(j, out, e)) { err = path + ": " + e; return false; }
  return true;
}

bool savePrefab(const std::string& path, const PrefabAsset& p, std::string& err) {
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) { err = "cannot write " + tmp; return false; }
    f << toJson(p).dump(1) << "\n";
    if (!f) { err = "write failed: " + tmp; return false; }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) { err = "cannot replace " + path; std::remove(tmp.c_str()); return false; }
  return true;
}

std::vector<std::string> validate(const PrefabAsset& p) {
  std::vector<std::string> r;
  auto checkMods = [&](const PrefabModifications& m, const std::string& who) {
    for (const PrefabOverride& o : m.overrides) {
      if (!validOverridePath(o.path)) r.push_back(who + ": bad override path '" + o.path + "'");
      if (o.occurrence < 0) r.push_back(who + ": negative occurrence on '" + o.path + "'");
    }
    for (const PrefabRemovedComponent& c : m.removedComponents) if (c.occurrence < 0) r.push_back(who + ": negative occurrence on removed " + c.type);
  };
  if (p.isVariant()) {
    if (!p.objects.empty()) r.push_back("a variant has no objects of its own");
    if (p.variantBase == p.guid && !p.guid.empty()) r.push_back("a variant cannot be its own base");
    checkMods(p.variant, "variant");
    return r;
  }
  if (p.rootFileId.empty()) r.push_back("rootFileId is missing");
  std::set<FileId> ids;
  int roots = 0;
  for (const PrefabObject& o : p.objects) {
    if (o.fileId.find('/') != std::string::npos) r.push_back("object id " + o.fileId + " must be a plain integer");
    if (!ids.insert(o.fileId).second) r.push_back("duplicate object id " + o.fileId);
    if (o.parent.empty()) ++roots;
    if (!allDigits(o.fileId)) continue;
    if (std::stoll(o.fileId) >= p.nextFileId) r.push_back("object id " + o.fileId + " is not below nextFileId");
    std::set<FileId> cids;
    for (const PrefabComponent& c : o.components) {
      if (!cids.insert(c.fileId).second) r.push_back("object " + o.fileId + ": duplicate component id " + c.fileId);
      if (c.type.empty()) r.push_back("object " + o.fileId + ": a component has no type");
    }
    checkMods(o.nested, "object " + o.fileId);
    if (o.nestedAsset.empty() && !o.nested.empty()) r.push_back("object " + o.fileId + ": modifications without a nested prefab");
    if (o.nestedAsset == p.guid && !p.guid.empty()) r.push_back("object " + o.fileId + ": a prefab cannot contain itself");
  }
  for (const PrefabObject& o : p.objects)
    if (!o.parent.empty() && !ids.count(o.parent)) r.push_back("object " + o.fileId + ": parent " + o.parent + " does not exist");
  if (roots != 1 && !p.objects.empty()) r.push_back("a prefab has exactly one root object (found " + std::to_string(roots) + ")");
  if (!p.rootFileId.empty() && !ids.empty() && !ids.count(p.rootFileId)) r.push_back("rootFileId " + p.rootFileId + " is not an object");
  if (p.objects.empty() && !p.rootFileId.empty()) r.push_back("a base prefab needs objects");
  return r;
}

std::string instanceObjectGuid(const std::string& rootGuid, const FileId& memberFileId) {
  // Two FNV-1a 64-bit passes with different seeds over "root|member"; formatted as a version-5-looking UUID.
  const std::string in = rootGuid + "|" + memberFileId;
  uint64_t h[2] = {1469598103934665603ULL, 0x9E3779B97F4A7C15ULL};
  for (int k = 0; k < 2; ++k)
    for (unsigned char c : in) { h[k] ^= c; h[k] *= 1099511628211ULL; }
  h[1] ^= h[0] >> 29;
  h[1] *= 1099511628211ULL;
  uint8_t b[16];
  for (int i = 0; i < 8; ++i) { b[i] = static_cast<uint8_t>(h[0] >> (8 * i)); b[8 + i] = static_cast<uint8_t>(h[1] >> (8 * i)); }
  b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x50);
  b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return buf;
}

}  // namespace editor
}  // namespace rensga
