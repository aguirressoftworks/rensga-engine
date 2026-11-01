#include "rensga/cooker/prefab_expand.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace rensga {
namespace editor {

using nlohmann::json;
using nlohmann::ordered_json;

namespace {

ordered_json toOrdered(const json& j) { return ordered_json::parse(j.dump()); }

struct FlatComp {
  std::string type;
  ordered_json fields = ordered_json::object();
  std::string addedId;
};

struct FlatObj {
  FileId id, alias, parent;
  std::string name;
  bool active = true;
  std::string tag;
  int layer = 0;
  ordered_json transform;
  std::vector<FlatComp> comps;
};
using Flat = std::vector<FlatObj>;

struct Ctx {
  const PrefabLoader& load;
  const ExpandOptions& opt;
  std::vector<std::string> stack;
  std::string err;
};

std::string nameOf(const Ctx& c, const std::string& guid) {
  if (c.opt.namer) {
    std::string n = c.opt.namer(guid);
    if (!n.empty()) return n;
  }
  return guid;
}

void warn(const Ctx& c, const std::string& msg) {
  if (c.opt.warnings) c.opt.warnings->push_back(msg);
}

ordered_json arrN(const float* v, int n) {
  ordered_json a = ordered_json::array();
  for (int i = 0; i < n; ++i) a.push_back(v[i]);
  return a;
}

ordered_json transformOf(const PrefabObject& o) {
  ordered_json t = ordered_json::object();
  t["position"] = arrN(o.position, 3);
  t["rotation"] = arrN(o.rotation, 4);
  t["scale"] = arrN(o.scale, 3);
  return t;
}

// "@7" and "@4/7": a reference from a component field to another object of the same prefab (written by "create prefab").
bool isToken(const std::string& s) {
  if (s.size() < 2 || s[0] != '@') return false;
  if (!std::isdigit(static_cast<unsigned char>(s[1]))) return false;
  for (size_t i = 1; i < s.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(s[i])) && s[i] != '/') return false;
  return true;
}

template <class F>
void mapStrings(ordered_json& j, const F& fn) {
  if (j.is_string()) {
    std::string s = j.get<std::string>();
    std::string r = fn(s);
    if (r != s) j = r;
  } else if (j.is_array() || j.is_object()) {
    for (auto& v : j) mapStrings(v, fn);
  }
}

FlatObj* findObj(Flat& f, const FileId& id) {
  for (FlatObj& o : f)
    if (o.id == id || (!o.alias.empty() && o.alias == id)) return &o;
  return nullptr;
}

void removeWithChildren(Flat& f, const FileId& id) {
  std::set<FileId> gone{id};
  bool grew = true;
  while (grew) {
    grew = false;
    for (const FlatObj& o : f)
      if (!o.parent.empty() && gone.count(o.parent) && !gone.count(o.id)) { gone.insert(o.id); grew = true; }
  }
  f.erase(std::remove_if(f.begin(), f.end(), [&](const FlatObj& o) { return gone.count(o.id) > 0; }), f.end());
}

// Pre-order, siblings in their current order; anything unreachable keeps its place at the end.
void sortPreOrder(Flat& f) {
  Flat out;
  std::set<FileId> done;
  auto visit = [&](auto&& self, const FileId& id) -> void {
    for (const FlatObj& o : f)
      if (o.id == id && !done.count(o.id)) {
        done.insert(o.id);
        out.push_back(o);
        for (const FlatObj& k : f)
          if (k.parent == o.id) self(self, k.id);
        return;
      }
  };
  for (const FlatObj& o : f)
    if (o.parent.empty()) visit(visit, o.id);
  for (const FlatObj& o : f)
    if (!done.count(o.id)) { done.insert(o.id); out.push_back(o); }
  f = std::move(out);
}

std::vector<std::string> splitDots(const std::string& s) {
  std::vector<std::string> v;
  std::string cur;
  for (char c : s) {
    if (c == '.') { v.push_back(cur); cur.clear(); }
    else cur += c;
  }
  v.push_back(cur);
  return v;
}

int partIndex(const std::string& seg) {
  if (seg == "x") return 0;
  if (seg == "y") return 1;
  if (seg == "z") return 2;
  if (seg == "w") return 3;
  if (seg.empty()) return -1;
  for (char c : seg) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
  return std::stoi(seg);
}

bool applyOverride(Ctx& c, Flat& flat, const PrefabOverride& ov, const std::string& who) {
  FlatObj* o = findObj(flat, ov.fileId);
  if (!o) { warn(c, who + ": override " + ov.path + " targets object " + ov.fileId + " which no longer exists"); return false; }
  if (!ov.path.empty() && ov.path[0] == '@') {
    const std::string k = ov.path.substr(1);
    if (k == "name" && ov.value.is_string()) o->name = ov.value.get<std::string>();
    else if (k == "active" && ov.value.is_boolean()) o->active = ov.value.get<bool>();
    else if (k == "tag" && ov.value.is_string()) o->tag = ov.value.get<std::string>();
    else if (k == "layer" && ov.value.is_number()) o->layer = ov.value.get<int>();
    else if (k == "position" || k == "rotation" || k == "scale") {
      if (!o->transform.is_object()) o->transform = ordered_json::object();
      o->transform[k] = toOrdered(ov.value);
    } else {
      warn(c, who + ": cannot apply " + ov.path);
      return false;
    }
    return true;
  }
  std::string type, rest;
  if (!splitOverridePath(ov.path, type, rest)) { warn(c, who + ": bad override path " + ov.path); return false; }
  int seen = 0;
  for (FlatComp& fc : o->comps) {
    if (fc.type != type) continue;
    if (seen++ == ov.occurrence) {
      setFieldPath(fc.fields, rest, ov.value);
      return true;
    }
  }
  warn(c, who + ": override " + ov.path + " targets a " + type + " component that no longer exists");
  return false;
}

void applyMods(Ctx& c, Flat& flat, const PrefabModifications& m, bool sceneOwner, const std::string& who) {
  for (const FileId& id : m.removedObjects) {
    FlatObj* o = findObj(flat, id);
    if (!o) { warn(c, who + ": removed object " + id + " does not exist"); continue; }
    removeWithChildren(flat, o->id);
  }
  for (const PrefabRemovedComponent& rc : m.removedComponents) {
    FlatObj* o = findObj(flat, rc.fileId);
    if (!o) { warn(c, who + ": component removal targets missing object " + rc.fileId); continue; }
    int seen = 0;
    bool done = false;
    for (size_t i = 0; i < o->comps.size() && !done; ++i)
      if (o->comps[i].type == rc.type && seen++ == rc.occurrence) { o->comps.erase(o->comps.begin() + static_cast<long>(i)); done = true; }
    if (!done) warn(c, who + ": removed " + rc.type + " component not found on object " + rc.fileId);
  }
  if (!sceneOwner && m.addedObjects.is_array()) {
    for (const json& entry : m.addedObjects) {
      json w = {{"format", kPrefabFormat}, {"rootFileId", "x"}, {"objects", json::array({entry})}};
      PrefabAsset pa;
      std::string e;
      if (!fromJson(w, pa, e) || pa.objects.size() != 1) { warn(c, who + ": bad added object (" + e + ")"); continue; }
      const PrefabObject& po = pa.objects[0];
      if (findObj(flat, po.fileId)) { warn(c, who + ": added object id " + po.fileId + " collides with an existing id"); continue; }
      if (!po.nestedAsset.empty()) { warn(c, who + ": an added object cannot be a nested prefab"); continue; }
      FlatObj f;
      f.id = po.fileId;
      f.parent = po.parent.empty() ? flat.front().id : po.parent;
      f.name = po.name; f.active = po.active; f.tag = po.tag; f.layer = po.layer;
      f.transform = transformOf(po);
      for (const PrefabComponent& pc : po.components) f.comps.push_back({pc.type, toOrdered(pc.fields), pc.fileId});
      flat.push_back(std::move(f));
    }
  }
  for (const PrefabAddedComponent& ac : m.addedComponents) {
    FlatObj* o = findObj(flat, ac.fileId);
    if (!o) { warn(c, who + ": added component targets missing object " + ac.fileId); continue; }
    o->comps.push_back({ac.component.type, toOrdered(ac.component.fields), ac.component.fileId});
  }
  for (const PrefabOverride& ov : m.overrides) applyOverride(c, flat, ov, who);
  sortPreOrder(flat);
}

bool expandAsset(Ctx& c, const std::string& guid, Flat& out) {
  if (std::find(c.stack.begin(), c.stack.end(), guid) != c.stack.end()) {
    std::string chain;
    for (const std::string& g : c.stack) chain += nameOf(c, g) + " -> ";
    c.err = "prefab cycle: " + chain + nameOf(c, guid);
    return false;
  }
  if (static_cast<int>(c.stack.size()) >= kPrefabMaxNesting) {
    c.err = "prefab nesting deeper than " + std::to_string(kPrefabMaxNesting) + " at " + nameOf(c, guid);
    return false;
  }
  PrefabAsset a;
  std::string e;
  if (!c.load(guid, a, e)) { c.err = "prefab " + nameOf(c, guid) + " cannot be loaded: " + e; return false; }
  c.stack.push_back(guid);
  bool ok = true;
  if (a.isVariant()) {
    Flat f;
    ok = expandAsset(c, a.variantBase, f);
    if (ok) {
      applyMods(c, f, a.variant, false, "variant " + nameOf(c, guid));
      out = std::move(f);
    }
  } else {
    Flat f;
    for (const PrefabObject& o : a.objects) {
      if (o.nestedAsset.empty()) {
        FlatObj g;
        g.id = o.fileId; g.parent = o.parent; g.name = o.name; g.active = o.active; g.tag = o.tag; g.layer = o.layer;
        g.transform = transformOf(o);
        for (const PrefabComponent& pc : o.components) g.comps.push_back({pc.type, toOrdered(pc.fields), ""});
        f.push_back(std::move(g));
        continue;
      }
      Flat n;
      if (!expandAsset(c, o.nestedAsset, n)) { ok = false; break; }
      applyMods(c, n, o.nested, false, "nested " + nameOf(c, o.nestedAsset) + " in " + nameOf(c, guid));
      FileId nestedRoot;
      for (const FlatObj& x : n) if (x.parent.empty()) { nestedRoot = x.id; break; }
      for (FlatObj x : n) {
        const bool isRoot = x.parent.empty();
        const FileId oldId = x.id;
        for (FlatComp& fc : x.comps)
          mapStrings(fc.fields, [&](const std::string& s) { return isToken(s) ? "@" + o.fileId + "/" + s.substr(1) : s; });
        if (isRoot) {
          x.alias = o.fileId + "/" + oldId;
          x.id = o.fileId;
          x.parent = o.parent;
          if (!o.name.empty()) x.name = o.name;
          x.active = o.active;
          if (!o.tag.empty()) x.tag = o.tag;
          if (o.layer != 0) x.layer = o.layer;
          x.transform = transformOf(o);
          for (const PrefabComponent& pc : o.components) x.comps.push_back({pc.type, toOrdered(pc.fields), ""});
        } else {
          x.id = o.fileId + "/" + oldId;
          x.parent = x.parent == nestedRoot ? o.fileId : o.fileId + "/" + x.parent;
          if (!x.alias.empty()) x.alias = o.fileId + "/" + x.alias;
        }
        f.push_back(std::move(x));
      }
    }
    if (ok) {
      sortPreOrder(f);
      out = std::move(f);
    }
  }
  c.stack.pop_back();
  return ok;
}

bool hasPrefabKey(const json& o) { return o.is_object() && o.contains("prefab") && !o["prefab"].is_null(); }

}  // namespace

bool splitOverridePath(const std::string& path, std::string& type, std::string& rest) {
  if (path.empty() || path[0] == '@') return false;
  const size_t dot = path.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= path.size()) return false;
  type = path.substr(0, dot);
  rest = path.substr(dot + 1);
  return true;
}

void setFieldPath(ordered_json& fields, const std::string& rest, const json& value) {
  if (!fields.is_object()) fields = ordered_json::object();
  const std::vector<std::string> segs = splitDots(rest);
  if (segs.size() == 1) { fields[segs[0]] = toOrdered(value); return; }
  ordered_json* cur = &fields[segs[0]];
  for (size_t i = 1; i < segs.size(); ++i) {
    const int idx = partIndex(segs[i]);
    if (idx < 0) return;
    const bool last = i + 1 == segs.size();
    if (!cur->is_array()) *cur = ordered_json::array();
    const bool nextIsPart = !last && partIndex(segs[i + 1]) >= 0;
    while (static_cast<int>(cur->size()) <= idx) {
      if (nextIsPart) cur->push_back(ordered_json::array({0, 0, 0}));
      else cur->push_back(0);
    }
    if (last) (*cur)[static_cast<size_t>(idx)] = toOrdered(value);
    else cur = &(*cur)[static_cast<size_t>(idx)];
  }
}

json getFieldPath(const json& fields, const std::string& rest) {
  const std::vector<std::string> segs = splitDots(rest);
  const json* cur = &fields;
  for (size_t i = 0; i < segs.size(); ++i) {
    if (i == 0) {
      if (!cur->is_object() || !cur->contains(segs[0])) return json();
      cur = &(*cur)[segs[0]];
    } else {
      const int idx = partIndex(segs[i]);
      if (idx < 0 || !cur->is_array() || idx >= static_cast<int>(cur->size())) return json();
      cur = &(*cur)[static_cast<size_t>(idx)];
    }
  }
  return *cur;
}

bool hasPrefabInstances(const json& objects) {
  if (!objects.is_array()) return false;
  for (const json& o : objects) if (hasPrefabKey(o)) return true;
  return false;
}

bool expandInstance(const ordered_json& rootObject, const PrefabLoader& load, std::vector<ordered_json>& out,
                    std::map<std::string, MemberInfo>* members, std::string& err, const ExpandOptions& opt) {
  PrefabInstance inst;
  std::string e;
  const std::string rootGuid = rootObject.value("guid", std::string());
  const std::string rootName = rootObject.value("name", std::string());
  if (!readInstanceKeys(rootObject, inst, e)) { err = "instance " + rootName + ": " + e; return false; }
  Ctx c{load, opt, {}, {}};
  Flat flat;
  if (!expandAsset(c, inst.asset, flat)) { err = "instance " + rootName + ": " + c.err; return false; }
  applyMods(c, flat, inst.mods, true, "instance " + rootName);
  if (flat.empty()) { err = "instance " + rootName + ": the prefab has no objects"; return false; }

  const FileId rootId = flat.front().id;
  std::map<FileId, std::string> guidOf;
  for (const FlatObj& f : flat) {
    const std::string g = f.id == rootId ? rootGuid : instanceObjectGuid(rootGuid, f.id);
    guidOf[f.id] = g;
    if (!f.alias.empty()) guidOf[f.alias] = g;
  }
  auto remapToken = [&](const std::string& s) -> std::string {
    if (!isToken(s)) return s;
    auto it = guidOf.find(s.substr(1));
    return it == guidOf.end() ? s : it->second;
  };

  std::vector<ordered_json> made;
  for (const FlatObj& f : flat) {
    const bool isRoot = f.id == rootId;
    ordered_json o = ordered_json::object();
    o["guid"] = guidOf[f.id];
    o["name"] = isRoot ? rootName : f.name;
    if (isRoot) o["parent"] = rootObject.contains("parent") ? rootObject["parent"] : ordered_json(nullptr);
    else o["parent"] = guidOf.count(f.parent) ? ordered_json(guidOf[f.parent]) : ordered_json(nullptr);
    o["active"] = isRoot ? rootObject.value("active", true) : f.active;
    if (!f.tag.empty()) o["tag"] = f.tag;
    if (f.layer != 0) o["layer"] = f.layer;
    o["transform"] = isRoot && rootObject.contains("transform") ? rootObject["transform"] : f.transform;
    ordered_json comps = ordered_json::array();
    MemberInfo mi;
    mi.rootGuid = rootGuid;
    mi.fileId = f.id;
    mi.isRoot = isRoot;
    std::map<std::string, int> occ;
    for (const FlatComp& fc : f.comps) {
      ordered_json cj = ordered_json::object();
      cj["type"] = fc.type;
      ordered_json fields = fc.fields;
      mapStrings(fields, remapToken);
      cj["fields"] = std::move(fields);
      comps.push_back(std::move(cj));
      mi.comps.push_back({fc.type, occ[fc.type]++, fc.addedId});
    }
    o["components"] = std::move(comps);
    if (members) (*members)[guidOf[f.id]] = std::move(mi);
    made.push_back(std::move(o));
  }
  for (ordered_json& o : made) out.push_back(std::move(o));

  if (inst.mods.addedObjects.is_array()) {
    for (const json& entryJ : inst.mods.addedObjects) {
      ordered_json entry = toOrdered(entryJ);
      if (hasPrefabKey(entry)) {
        std::string e2;
        if (!expandInstance(entry, load, out, members, e2, opt)) { err = "instance " + rootName + ": " + e2; return false; }
        if (members) {  // the nested root is also an object added to this instance
          auto it = members->find(entry.value("guid", std::string()));
          if (it != members->end()) { it->second.added = true; it->second.outerGuid = rootGuid; }
        }
        continue;
      }
      MemberInfo mi;
      mi.rootGuid = rootGuid;
      mi.added = true;
      std::map<std::string, int> occ;
      if (entry.contains("components") && entry["components"].is_array())
        for (const ordered_json& cj : entry["components"]) {
          const std::string t = cj.value("type", std::string());
          mi.comps.push_back({t, occ[t]++, ""});
        }
      if (members) (*members)[entry.value("guid", std::string())] = std::move(mi);
      out.push_back(std::move(entry));
    }
  }
  return true;
}

bool expandSceneObjects(const ordered_json& objects, const PrefabLoader& load, ordered_json& out, std::map<std::string, MemberInfo>* members,
                        std::vector<std::string>& errors, const ExpandOptions& opt) {
  out = ordered_json::array();
  if (!objects.is_array()) return true;
  for (const ordered_json& o : objects) {
    if (!hasPrefabKey(o)) { out.push_back(o); continue; }
    std::vector<ordered_json> made;
    std::string err;
    if (expandInstance(o, load, made, members, err, opt)) {
      for (ordered_json& m : made) out.push_back(std::move(m));
    } else {
      errors.push_back(err);
      ordered_json plain = o;  // keep the root so the scene still opens; the members are missing
      out.push_back(std::move(plain));
    }
  }
  return errors.empty();
}

std::vector<std::string> checkPrefab(const std::string& guid, const PrefabLoader& load, const PrefabNamer& namer) {
  std::vector<std::string> problems;
  ExpandOptions opt;
  opt.namer = namer;
  opt.warnings = &problems;
  Ctx c{load, opt, {}, {}};
  Flat f;
  if (!expandAsset(c, guid, f)) problems.push_back(c.err);
  return problems;
}

bool expandPrefabTemplate(const std::string& prefabGuid, const std::string& rootGuid, const PrefabLoader& load,
                          std::vector<ordered_json>& out, std::string& err) {
  ExpandOptions opt;
  Ctx c{load, opt, {}, {}};
  Flat f;
  if (!expandAsset(c, prefabGuid, f)) { err = c.err; return false; }
  if (f.empty()) { err = "the prefab has no objects"; return false; }
  ordered_json root = ordered_json::object();
  root["guid"] = rootGuid;
  root["name"] = f.front().name;
  root["parent"] = nullptr;
  root["active"] = true;
  root["transform"] = f.front().transform;
  PrefabInstance inst;
  inst.asset = prefabGuid;
  inst.fileId = f.front().id;
  writeInstanceKeys(root, inst);
  std::vector<ordered_json> made;
  if (!expandInstance(root, load, made, nullptr, err, opt)) return false;
  if (!made.empty()) made.front()["active"] = false;  // a template: scripts Instantiate it and SetActive(true)
  for (ordered_json& m : made) out.push_back(std::move(m));
  return true;
}

}  // namespace editor
}  // namespace rensga
