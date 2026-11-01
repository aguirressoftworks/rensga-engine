#include "rensga/editor/prefab_edit.h"

#include <algorithm>
#include <set>

namespace rensga {
namespace editor {

using nlohmann::json;
using nlohmann::ordered_json;

namespace {

ordered_json toOrdered(const json& j) { return ordered_json::parse(j.dump()); }
json toPlain(const ordered_json& j) { return json::parse(j.dump()); }

bool readArr(const json& j, const char* key, float* v, int n) {
  if (!j.contains(key) || !j[key].is_array() || static_cast<int>(j[key].size()) != n) return false;
  for (int i = 0; i < n; ++i) if (!j[key][static_cast<size_t>(i)].is_number()) return false;
  for (int i = 0; i < n; ++i) v[i] = j[key][static_cast<size_t>(i)].get<float>();
  return true;
}

ordered_json arrN(const float* v, int n) {
  ordered_json a = ordered_json::array();
  for (int i = 0; i < n; ++i) a.push_back(v[i]);
  return a;
}

bool isToken(const std::string& s) {
  if (s.size() < 2 || s[0] != '@' || !std::isdigit(static_cast<unsigned char>(s[1]))) return false;
  for (size_t i = 1; i < s.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(s[i])) && s[i] != '/') return false;
  return true;
}

template <class J, class F>
void mapStr(J& j, const F& fn) {
  if (j.is_string()) {
    const std::string s = j.template get<std::string>();
    const std::string r = fn(s);
    if (r != s) j = r;
  } else if (j.is_array() || j.is_object()) {
    for (auto& v : j) mapStr(v, fn);
  }
}

std::vector<std::string> splitSlash(const std::string& s) {
  std::vector<std::string> v;
  std::string cur;
  for (char c : s) { if (c == '/') { v.push_back(cur); cur.clear(); } else cur += c; }
  v.push_back(cur);
  return v;
}

}  // namespace

std::string stageObjectGuid(const std::string& prefabGuid, const FileId& id) { return instanceObjectGuid(prefabGuid, "stage/" + id); }

// ---------------------------------------------------------------- create

bool prefabFromSceneObjects(const std::vector<ordered_json>& subtree, const std::string& prefabGuid, PrefabAsset& out,
                            std::map<std::string, FileId>& idOf, std::string& err) {
  if (subtree.empty()) { err = "nothing to make a prefab from"; return false; }
  PrefabAsset a;
  a.guid = prefabGuid;
  idOf.clear();
  for (const ordered_json& o : subtree) idOf[o.value("guid", std::string())] = a.allocFileId();
  a.rootFileId = idOf[subtree.front().value("guid", std::string())];
  for (const ordered_json& oj : subtree) {
    const std::string g = oj.value("guid", std::string());
    PrefabObject o;
    o.fileId = idOf[g];
    o.name = oj.value("name", std::string());
    if (&oj != &subtree.front() && oj.contains("parent") && oj["parent"].is_string()) {
      auto it = idOf.find(oj["parent"].get<std::string>());
      o.parent = it == idOf.end() ? a.rootFileId : it->second;
    }
    o.active = oj.value("active", true);
    o.tag = oj.value("tag", std::string());
    o.layer = oj.value("layer", 0);
    if (oj.contains("transform") && oj["transform"].is_object()) {
      const json t = toPlain(oj["transform"]);
      readArr(t, "position", o.position, 3);
      readArr(t, "rotation", o.rotation, 4);
      readArr(t, "scale", o.scale, 3);
    }
    if (oj.contains("prefab") && !oj["prefab"].is_null()) {
      PrefabInstance inst;
      std::string e;
      if (!readInstanceKeys(toPlain(oj), inst, e)) { err = o.name + ": " + e; return false; }
      if (inst.mods.addedObjects.is_array() && !inst.mods.addedObjects.empty()) {
        err = "'" + o.name + "' has objects added to its prefab instance; apply or revert them before making a prefab around it";
        return false;
      }
      o.nestedAsset = inst.asset;
      o.nestedRootFileId = inst.fileId;
      o.nested = inst.mods;
    } else if (oj.contains("components") && oj["components"].is_array()) {
      for (const ordered_json& cj : oj["components"]) {
        PrefabComponent pc;
        pc.fileId = a.allocFileId();
        pc.type = cj.value("type", std::string());
        pc.fields = cj.contains("fields") ? toPlain(cj["fields"]) : json::object();
        mapStr(pc.fields, [&](const std::string& s) { auto it = idOf.find(s); return it == idOf.end() ? s : "@" + it->second; });
        o.components.push_back(std::move(pc));
      }
    }
    a.objects.push_back(std::move(o));
  }
  out = std::move(a);
  return true;
}

ordered_json makeInstanceRoot(const std::string& guid, const std::string& name, const std::string& parentGuid, const PrefabAsset& prefab,
                              const std::string& prefabGuid) {
  ordered_json o = ordered_json::object();
  o["guid"] = guid;
  o["name"] = name;
  o["parent"] = parentGuid.empty() ? ordered_json(nullptr) : ordered_json(parentGuid);
  o["active"] = true;
  ordered_json t = ordered_json::object();
  const PrefabObject* root = prefab.find(prefab.rootFileId);
  const float p[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  t["position"] = arrN(root ? root->position : p, 3);
  t["rotation"] = arrN(root ? root->rotation : r, 4);
  t["scale"] = arrN(root ? root->scale : s, 3);
  o["transform"] = std::move(t);
  o["components"] = ordered_json::array();
  PrefabInstance inst;
  inst.asset = prefabGuid;
  inst.fileId = prefab.rootFileId.empty() ? FileId("0") : prefab.rootFileId;
  writeInstanceKeys(o, inst);
  return o;
}

// ---------------------------------------------------------------- apply

namespace {

struct Target {
  PrefabObject* obj = nullptr;       // modify this object directly
  PrefabModifications* mods = nullptr;  // or record a modification here, with the id `rel`
  FileId rel;
};

Target resolve(PrefabAsset& a, const FileId& id, const std::string& componentType) {
  Target t;
  if (a.isVariant()) { t.mods = &a.variant; t.rel = id; return t; }
  const size_t slash = id.find('/');
  if (slash != std::string::npos) {
    PrefabObject* entry = nullptr;
    for (PrefabObject& o : a.objects) if (o.fileId == id.substr(0, slash)) entry = &o;
    if (entry && !entry->nestedAsset.empty()) { t.mods = &entry->nested; t.rel = id.substr(slash + 1); }
    return t;
  }
  for (PrefabObject& o : a.objects) {
    if (o.fileId != id) continue;
    bool own = componentType.empty();
    for (const PrefabComponent& c : o.components) own = own || c.type == componentType;
    if (!o.nestedAsset.empty() && !own) { t.mods = &o.nested; t.rel = o.nestedRootFileId; }
    else t.obj = &o;
    return t;
  }
  return t;
}

bool sameOverride(const PrefabOverride& a, const PrefabOverride& b) {
  return a.fileId == b.fileId && a.path == b.path && a.occurrence == b.occurrence;
}

void setOverride(PrefabModifications& m, PrefabOverride ov) {
  for (PrefabOverride& e : m.overrides) if (sameOverride(e, ov)) { e.value = ov.value; return; }
  m.overrides.push_back(std::move(ov));
}

PrefabComponent* nthComponent(PrefabObject& o, const std::string& type, int occurrence) {
  int seen = 0;
  for (PrefabComponent& c : o.components) if (c.type == type && seen++ == occurrence) return &c;
  return nullptr;
}

bool applyObjectProp(PrefabObject& o, const std::string& path, const json& v) {
  if (path == "@name" && v.is_string()) o.name = v.get<std::string>();
  else if (path == "@active" && v.is_boolean()) o.active = v.get<bool>();
  else if (path == "@tag" && v.is_string()) o.tag = v.get<std::string>();
  else if (path == "@layer" && v.is_number()) o.layer = v.get<int>();
  else if (path == "@position") return readArr(json{{"v", v}}, "v", o.position, 3);
  else if (path == "@rotation") return readArr(json{{"v", v}}, "v", o.rotation, 4);
  else if (path == "@scale") return readArr(json{{"v", v}}, "v", o.scale, 3);
  else return false;
  return true;
}

void applyOneOverride(PrefabAsset& a, const PrefabOverride& ov, std::vector<std::string>& warnings) {
  std::string type, rest;
  const bool isObjProp = !ov.path.empty() && ov.path[0] == '@';
  if (!isObjProp && !splitOverridePath(ov.path, type, rest)) { warnings.push_back("bad override path " + ov.path); return; }
  Target t = resolve(a, ov.fileId, isObjProp ? std::string() : type);
  if (t.obj) {
    if (isObjProp) {
      if (!applyObjectProp(*t.obj, ov.path, ov.value)) warnings.push_back("cannot apply " + ov.path);
      return;
    }
    PrefabComponent* c = nthComponent(*t.obj, type, ov.occurrence);
    if (!c) { warnings.push_back("object " + ov.fileId + " has no " + type + " component"); return; }
    ordered_json f = toOrdered(c->fields);
    setFieldPath(f, rest, ov.value);
    c->fields = toPlain(f);
    return;
  }
  if (t.mods) {
    PrefabOverride n = ov;
    n.fileId = t.rel;
    setOverride(*t.mods, std::move(n));
    return;
  }
  warnings.push_back("object " + ov.fileId + " does not exist in the prefab");
}

}  // namespace

bool applyModsToPrefab(PrefabAsset& a, const PrefabModifications& mods, const std::map<std::string, FileId>& guidToFileId,
                       std::vector<std::string>& warnings) {
  for (const PrefabOverride& ov : mods.overrides) applyOneOverride(a, ov, warnings);
  for (const PrefabAddedComponent& ac : mods.addedComponents) {
    Target t = resolve(a, ac.fileId, std::string());
    PrefabComponent pc;
    pc.fileId = a.allocFileId();
    pc.type = ac.component.type;
    pc.fields = ac.component.fields;
    if (t.obj) t.obj->components.push_back(std::move(pc));
    else if (t.mods) {
      PrefabAddedComponent n;
      n.fileId = t.rel;
      n.component = std::move(pc);
      t.mods->addedComponents.push_back(std::move(n));
    } else warnings.push_back("added component targets missing object " + ac.fileId);
  }
  for (const PrefabRemovedComponent& rc : mods.removedComponents) {
    Target t = resolve(a, rc.fileId, rc.type);
    if (t.obj) {
      int seen = 0;
      bool done = false;
      for (size_t i = 0; i < t.obj->components.size() && !done; ++i)
        if (t.obj->components[i].type == rc.type && seen++ == rc.occurrence) { t.obj->components.erase(t.obj->components.begin() + static_cast<long>(i)); done = true; }
      if (!done) warnings.push_back("no " + rc.type + " component to remove on object " + rc.fileId);
    } else if (t.mods) {
      PrefabRemovedComponent n = rc;
      n.fileId = t.rel;
      t.mods->removedComponents.push_back(n);
    } else warnings.push_back("component removal targets missing object " + rc.fileId);
  }
  for (const FileId& id : mods.removedObjects) {
    Target t = resolve(a, id, std::string());
    if (t.mods && !a.isVariant() && t.rel != id) { t.mods->removedObjects.push_back(t.rel); continue; }
    if (a.isVariant()) { a.variant.removedObjects.push_back(id); continue; }
    if (!t.obj) { warnings.push_back("removed object " + id + " does not exist"); continue; }
    if (t.obj->fileId == a.rootFileId) { warnings.push_back("the root of a prefab cannot be removed"); continue; }
    std::set<FileId> gone{t.obj->fileId};
    for (bool grew = true; grew;) {
      grew = false;
      for (const PrefabObject& o : a.objects) if (!o.parent.empty() && gone.count(o.parent) && gone.insert(o.fileId).second) grew = true;
    }
    a.objects.erase(std::remove_if(a.objects.begin(), a.objects.end(), [&](const PrefabObject& o) { return gone.count(o.fileId) > 0; }), a.objects.end());
  }
  if (mods.addedObjects.is_array() && !mods.addedObjects.empty()) {
    std::map<std::string, FileId> ids = guidToFileId;
    for (const json& e : mods.addedObjects) {
      const std::string g = e.value("guid", std::string());
      if (e.contains("prefab") && !e["prefab"].is_null()) { warnings.push_back("object '" + e.value("name", std::string()) + "' is a prefab instance; add it to the prefab by hand"); continue; }
      PrefabObject o;
      o.fileId = a.isVariant() ? "v" + a.guid.substr(0, 8) + "_" + std::to_string(a.nextFileId++) : a.allocFileId();
      o.name = e.value("name", std::string());
      auto pit = e.contains("parent") && e["parent"].is_string() ? ids.find(e["parent"].get<std::string>()) : ids.end();
      o.parent = pit == ids.end() ? (a.isVariant() ? FileId() : a.rootFileId) : pit->second;
      o.active = e.value("active", true);
      o.tag = e.value("tag", std::string());
      o.layer = e.value("layer", 0);
      if (e.contains("transform")) {
        readArr(e["transform"], "position", o.position, 3);
        readArr(e["transform"], "rotation", o.rotation, 4);
        readArr(e["transform"], "scale", o.scale, 3);
      }
      if (e.contains("components") && e["components"].is_array())
        for (const json& cj : e["components"]) {
          PrefabComponent pc;
          pc.fileId = a.allocFileId();
          pc.type = cj.value("type", std::string());
          pc.fields = cj.value("fields", json::object());
          o.components.push_back(std::move(pc));
        }
      ids[g] = o.fileId;
      if (a.isVariant()) {
        PrefabAsset tmp;
        tmp.rootFileId = o.fileId;
        tmp.nextFileId = a.nextFileId;
        tmp.objects.push_back(o);
        if (!a.variant.addedObjects.is_array()) a.variant.addedObjects = json::array();
        a.variant.addedObjects.push_back(json::parse(toJson(tmp)["objects"][0].dump()));
      } else {
        a.objects.push_back(std::move(o));
      }
    }
  }
  return true;
}

void subtractMods(PrefabModifications& all, const PrefabModifications& part) {
  for (const PrefabOverride& p : part.overrides)
    all.overrides.erase(std::remove_if(all.overrides.begin(), all.overrides.end(), [&](const PrefabOverride& e) { return sameOverride(e, p); }), all.overrides.end());
  for (const PrefabAddedComponent& p : part.addedComponents)
    all.addedComponents.erase(std::remove_if(all.addedComponents.begin(), all.addedComponents.end(),
                                             [&](const PrefabAddedComponent& e) { return e.fileId == p.fileId && e.component.fileId == p.component.fileId; }),
                              all.addedComponents.end());
  for (const PrefabRemovedComponent& p : part.removedComponents)
    all.removedComponents.erase(std::remove_if(all.removedComponents.begin(), all.removedComponents.end(),
                                               [&](const PrefabRemovedComponent& e) { return e.fileId == p.fileId && e.type == p.type && e.occurrence == p.occurrence; }),
                                all.removedComponents.end());
  for (const FileId& p : part.removedObjects)
    all.removedObjects.erase(std::remove(all.removedObjects.begin(), all.removedObjects.end(), p), all.removedObjects.end());
  if (all.addedObjects.is_array() && part.addedObjects.is_array()) {
    std::set<std::string> gone;
    for (const json& e : part.addedObjects) gone.insert(e.value("guid", std::string()));
    json kept = json::array();
    for (const json& e : all.addedObjects) if (!gone.count(e.value("guid", std::string()))) kept.push_back(e);
    all.addedObjects = std::move(kept);
  }
}

// ---------------------------------------------------------------- stage

ordered_json prefabToStage(const PrefabAsset& a) {
  ordered_json st = ordered_json::object();
  st["format"] = 1;
  ordered_json meta = ordered_json::object();
  meta["guid"] = a.guid;
  meta["nextFileId"] = a.nextFileId;
  if (a.isVariant()) meta["variantBase"] = a.variantBase;
  else meta["rootFileId"] = fileIdToJson(a.rootFileId);
  st["prefabStage"] = std::move(meta);
  ordered_json objs = ordered_json::array();
  auto gOf = [&](const FileId& id) { return stageObjectGuid(a.guid, id); };
  auto tokenToGuid = [&](const std::string& s) -> std::string {
    if (!isToken(s)) return s;
    const std::vector<std::string> parts = splitSlash(s.substr(1));
    if (parts.size() == 1) return gOf(parts[0]);
    std::string rest = parts[1];
    for (size_t i = 2; i < parts.size(); ++i) rest += "/" + parts[i];
    return instanceObjectGuid(gOf(parts[0]), rest);
  };
  if (a.isVariant()) {
    ordered_json o = ordered_json::object();
    o["guid"] = gOf("variant");
    o["name"] = "Variant";
    o["parent"] = nullptr;
    o["active"] = true;
    ordered_json t = ordered_json::object();
    const float p[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    t["position"] = arrN(p, 3); t["rotation"] = arrN(r, 4); t["scale"] = arrN(s, 3);
    o["transform"] = std::move(t);
    o["components"] = ordered_json::array();
    PrefabInstance inst;
    inst.asset = a.variantBase;
    inst.fileId = "0";
    inst.mods = a.variant;
    json rawAdded = inst.mods.addedObjects;
    inst.mods.addedObjects = json::array();
    writeInstanceKeys(o, inst);
    if (rawAdded.is_array() && !rawAdded.empty()) o["_addedObjects"] = toOrdered(rawAdded);
    objs.push_back(std::move(o));
  } else {
    for (const PrefabObject& po : a.objects) {
      ordered_json o = ordered_json::object();
      o["guid"] = gOf(po.fileId);
      o["fileId"] = fileIdToJson(po.fileId);
      o["name"] = po.name;
      o["parent"] = po.parent.empty() ? ordered_json(nullptr) : ordered_json(gOf(po.parent));
      o["active"] = po.active;
      if (!po.tag.empty()) o["tag"] = po.tag;
      if (po.layer != 0) o["layer"] = po.layer;
      ordered_json t = ordered_json::object();
      t["position"] = arrN(po.position, 3); t["rotation"] = arrN(po.rotation, 4); t["scale"] = arrN(po.scale, 3);
      o["transform"] = std::move(t);
      ordered_json comps = ordered_json::array();
      for (const PrefabComponent& pc : po.components) {
        ordered_json cj = ordered_json::object();
        cj["type"] = pc.type;
        ordered_json f = toOrdered(pc.fields);
        mapStr(f, tokenToGuid);
        cj["fields"] = std::move(f);
        cj["fileId"] = fileIdToJson(pc.fileId);
        comps.push_back(std::move(cj));
      }
      o["components"] = std::move(comps);
      if (!po.nestedAsset.empty()) {
        PrefabInstance inst;
        inst.asset = po.nestedAsset;
        inst.fileId = po.nestedRootFileId;
        inst.mods = po.nested;
        json rawAdded = inst.mods.addedObjects;
        inst.mods.addedObjects = json::array();
        writeInstanceKeys(o, inst);
        if (rawAdded.is_array() && !rawAdded.empty()) o["_addedObjects"] = toOrdered(rawAdded);
      }
      objs.push_back(std::move(o));
    }
  }
  st["objects"] = std::move(objs);
  return st;
}

bool stageToPrefab(const ordered_json& stage, PrefabAsset& out, std::string& err, StageIds* sticky) {
  if (!stage.contains("prefabStage") || !stage["prefabStage"].is_object()) { err = "not a prefab stage"; return false; }
  const ordered_json& meta = stage["prefabStage"];
  PrefabAsset a;
  a.guid = meta.value("guid", std::string());
  a.nextFileId = meta.value("nextFileId", 1LL);
  const ordered_json& objs = stage.contains("objects") ? stage["objects"] : ordered_json::array();
  if (meta.contains("variantBase")) {
    a.variantBase = meta["variantBase"].get<std::string>();
    for (const ordered_json& o : objs) {
      if (!o.contains("prefab") || o["prefab"].is_null()) continue;
      PrefabInstance inst;
      std::string e;
      if (!readInstanceKeys(toPlain(o), inst, e)) { err = e; return false; }
      a.variant = inst.mods;
      if (o.contains("_addedObjects")) a.variant.addedObjects = toPlain(o["_addedObjects"]);
      out = std::move(a);
      return true;
    }
    err = "the variant's base instance is missing from the stage";
    return false;
  }
  // guid -> id; keep ids that are unique, give the others new ones
  std::map<std::string, FileId> idOf;
  std::set<FileId> used;
  long long next = a.nextFileId;
  if (sticky) next = std::max(next, sticky->next);
  for (const ordered_json& o : objs) {  // ids that stay: the ones the file gave, then the ones handed out before
    const FileId id = o.contains("fileId") ? fileIdFromJson(toPlain(o["fileId"])) : FileId();
    if (!id.empty() && id.find('/') == std::string::npos) used.insert(id);
    if (sticky) { auto it = sticky->objects.find(o.value("guid", std::string())); if (it != sticky->objects.end()) used.insert(it->second); }
  }
  for (const FileId& u : used) if (!u.empty() && std::isdigit(static_cast<unsigned char>(u[0])) && std::stoll(u) >= next) next = std::stoll(u) + 1;
  std::set<FileId> taken;
  for (const ordered_json& o : objs) {
    const std::string g = o.value("guid", std::string());
    FileId id = o.contains("fileId") ? fileIdFromJson(toPlain(o["fileId"])) : FileId();
    if (id.empty() && sticky) { auto it = sticky->objects.find(g); if (it != sticky->objects.end()) id = it->second; }
    if (id.empty() || taken.count(id) || id.find('/') != std::string::npos) id = std::to_string(next++);
    taken.insert(id);
    if (sticky) sticky->objects[g] = id;
    idOf[g] = id;
  }
  used = taken;
  for (const FileId& u : used) if (!u.empty() && std::isdigit(static_cast<unsigned char>(u[0])) && std::stoll(u) >= next) next = std::stoll(u) + 1;
  a.nextFileId = next;
  auto guidToToken = [&](const std::string& s) -> std::string {
    auto it = idOf.find(s);
    return it == idOf.end() ? s : "@" + it->second;
  };
  int roots = 0;
  for (const ordered_json& oj : objs) {
    PrefabObject o;
    o.fileId = idOf[oj.value("guid", std::string())];
    o.name = oj.value("name", std::string());
    if (oj.contains("parent") && oj["parent"].is_string()) {
      auto it = idOf.find(oj["parent"].get<std::string>());
      if (it != idOf.end()) o.parent = it->second;
    }
    if (o.parent.empty()) { ++roots; a.rootFileId = o.fileId; }
    o.active = oj.value("active", true);
    o.tag = oj.value("tag", std::string());
    o.layer = oj.value("layer", 0);
    if (oj.contains("transform")) {
      const json t = toPlain(oj["transform"]);
      readArr(t, "position", o.position, 3);
      readArr(t, "rotation", o.rotation, 4);
      readArr(t, "scale", o.scale, 3);
    }
    std::set<FileId> cused;
    if (oj.contains("components") && oj["components"].is_array())
      for (const ordered_json& cj : oj["components"]) {
        PrefabComponent pc;
        pc.type = cj.value("type", std::string());
        pc.fileId = cj.contains("fileId") ? fileIdFromJson(toPlain(cj["fileId"])) : FileId();
        if (pc.fileId.empty() || cused.count(pc.fileId)) pc.fileId = std::to_string(a.nextFileId++);
        cused.insert(pc.fileId);
        ordered_json f = cj.contains("fields") ? cj["fields"] : ordered_json::object();
        mapStr(f, guidToToken);
        pc.fields = toPlain(f);
        o.components.push_back(std::move(pc));
      }
    if (oj.contains("prefab") && !oj["prefab"].is_null()) {
      PrefabInstance inst;
      std::string e;
      if (!readInstanceKeys(toPlain(oj), inst, e)) { err = o.name + ": " + e; return false; }
      o.nestedAsset = inst.asset;
      o.nestedRootFileId = inst.fileId;
      o.nested = inst.mods;
      if (oj.contains("_addedObjects")) o.nested.addedObjects = toPlain(oj["_addedObjects"]);
    }
    a.objects.push_back(std::move(o));
  }
  if (roots != 1) { err = "a prefab has exactly one root object (the stage has " + std::to_string(roots) + ")"; return false; }
  if (sticky) sticky->next = a.nextFileId;
  out = std::move(a);
  return true;
}

}  // namespace editor
}  // namespace rensga
