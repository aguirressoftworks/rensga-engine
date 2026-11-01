// F6.01 (Editor 6): prefab behaviour of SceneDocument. The document stores instance roots; this file expands them into the view,
// records edits of members as overrides on the root (through the ordinary snapshot undo), and implements apply, revert, create,
// instantiate and unpack. The edit routing hooks sit at the top of the SceneDocument edit methods (scene_document.cpp).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "rensga/editor/asset_stubs.h"
#include "rensga/editor/prefab_edit.h"
#include "rensga/editor/prefab_library.h"
#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {

using nlohmann::json;
using nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {

json toPlain(const ordered_json& j) { return json::parse(j.dump()); }
ordered_json toOrdered(const json& j) { return ordered_json::parse(j.dump()); }

double quant(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.7g", static_cast<double>(v));
  return std::strtod(buf, nullptr);
}
json arrN(const float* v, int n) {
  json a = json::array();
  for (int i = 0; i < n; ++i) a.push_back(quant(v[i]));
  return a;
}

FileId fid(const ordered_json& e, const char* key = "fileId") {
  return e.contains(key) ? fileIdFromJson(toPlain(e[key])) : FileId();
}

void remapAll(ordered_json& j, const std::map<std::string, std::string>& m) {
  if (j.is_string()) {
    auto it = m.find(j.get_ref<const std::string&>());
    if (it != m.end()) j = it->second;
  } else if (j.is_array() || j.is_object()) {
    for (auto& v : j) remapAll(v, m);
  }
}

const char* const kModKeys[] = {"overrides", "addedComponents", "removedComponents", "removedObjects", "addedObjects"};

void tidy(ordered_json& root) {
  for (const char* k : kModKeys)
    if (root.contains(k) && root[k].is_array() && root[k].empty()) root.erase(k);
}

bool overrideMatches(const ordered_json& e, const FileId& id, const std::string& path, int occ, bool prefix) {
  if (fid(e) != id) return false;
  const std::string p = e.value("path", std::string());
  const bool objProp = !path.empty() && path[0] == '@';
  if (!objProp && e.value("occurrence", 0) != occ) return false;
  if (p == path) return true;
  return prefix && p.size() > path.size() && p.compare(0, path.size(), path) == 0 && p[path.size()] == '.';
}

void eraseOverrides(ordered_json& root, const FileId& id, const std::string& path, int occ, bool prefix) {
  if (!root.contains("overrides") || !root["overrides"].is_array()) return;
  ordered_json kept = ordered_json::array();
  for (ordered_json& e : root["overrides"]) if (!overrideMatches(e, id, path, occ, prefix)) kept.push_back(std::move(e));
  root["overrides"] = std::move(kept);
  tidy(root);
}

void setOverride(ordered_json& root, const FileId& id, const std::string& path, int occ, const ordered_json& value) {
  if (!root.contains("overrides") || !root["overrides"].is_array()) root["overrides"] = ordered_json::array();
  for (ordered_json& e : root["overrides"])
    if (overrideMatches(e, id, path, occ, false)) { e["value"] = value; return; }
  ordered_json e = ordered_json::object();
  e["fileId"] = toOrdered(fileIdToJson(id));
  e["path"] = path;
  if (path.empty() || path[0] != '@') if (occ != 0) e["occurrence"] = occ;
  e["value"] = value;
  root["overrides"].push_back(std::move(e));
}

ordered_json* searchAdded(ordered_json& obj, const std::string& guid) {
  if (!obj.contains("addedObjects") || !obj["addedObjects"].is_array()) return nullptr;
  for (ordered_json& e : obj["addedObjects"]) {
    if (e.value("guid", std::string()) == guid) return &e;
    if (ordered_json* r = searchAdded(e, guid)) return r;
  }
  return nullptr;
}

// One undo step for a prefab FILE change (apply to prefab): grouped with the scene snapshot by SceneDocument.
class PrefabFileCommand : public Command {
 public:
  PrefabFileCommand(PrefabLibrary* lib, std::string guid, std::string before, std::string after)
      : lib_(lib), guid_(std::move(guid)), before_(std::move(before)), after_(std::move(after)) {}
  const char* name() const override { return "Apply to prefab"; }
  void undo() override { std::string e; lib_->writeText(guid_, before_, e); }
  void redo() override { std::string e; lib_->writeText(guid_, after_, e); }

 private:
  PrefabLibrary* lib_;
  std::string guid_, before_, after_;
};

}  // namespace

// ---------------------------------------------------------------- view

void SceneDocument::setPrefabLibrary(PrefabLibrary* lib) {
  lib_ = lib;
  if (loaded_) { rebuildView(); }
}

void SceneDocument::rebuildView() {
  members_.clear();
  prefabProblems_.clear();
  baseCache_.clear();
  json plain = json::parse(doc_.dump());
  if (plain.contains("objects") && hasPrefabInstances(plain["objects"])) {
    if (lib_ && lib_->isOpen()) {
      ordered_json expanded;
      ExpandOptions opt;
      opt.namer = lib_->namer();
      expandSceneObjects(doc_["objects"], lib_->loader(), expanded, &members_, prefabProblems_, opt);
      plain["objects"] = toPlain(expanded);
    } else {
      prefabProblems_.push_back("no prefab library: prefab instances are not expanded");
    }
  }
  view_ = parseScene(plain);
  view_.path = path_.string();
  libRev_ = lib_ ? lib_->revision() : -1;
}

bool SceneDocument::refreshPrefabs() {
  if (!lib_ || mirror_ || !loaded_ || lib_->revision() == libRev_) return false;
  rebuildView();
  if (!selected_.empty() && indexOf(selected_) < 0) selected_.clear();
  extraSel_.erase(std::remove_if(extraSel_.begin(), extraSel_.end(), [&](const std::string& g) { return indexOf(g) < 0; }), extraSel_.end());
  if (selected_.empty() && !extraSel_.empty()) { selected_ = extraSel_.front(); extraSel_.erase(extraSel_.begin()); }
  return true;
}

const MemberInfo* SceneDocument::memberInfo(const std::string& guid) const {
  auto it = members_.find(guid);
  return it == members_.end() ? nullptr : &it->second;
}
bool SceneDocument::isInstanceRoot(const std::string& guid) const { const MemberInfo* m = memberInfo(guid); return m && m->isRoot; }
bool SceneDocument::isPrefabMember(const std::string& guid) const { const MemberInfo* m = memberInfo(guid); return m && !m->isRoot && !m->added; }
bool SceneDocument::isAddedToInstance(const std::string& guid) const { const MemberInfo* m = memberInfo(guid); return m && m->added && !m->isRoot; }
bool SceneDocument::inInstance(const std::string& guid) const { const MemberInfo* m = memberInfo(guid); return m && (m->added || !m->isRoot); }
std::string SceneDocument::instanceRootOf(const std::string& guid) const {
  const MemberInfo* m = memberInfo(guid);
  return !m ? std::string() : m->isRoot ? guid : m->rootGuid;
}
std::string SceneDocument::prefabOf(const std::string& guid) const {
  const std::string r = instanceRootOf(guid);
  if (r.empty()) return r;
  ordered_json* rj = const_cast<SceneDocument*>(this)->findInstanceJson(r);
  return rj && rj->contains("prefab") && (*rj)["prefab"].is_object() ? (*rj)["prefab"].value("asset", std::string()) : std::string();
}

ordered_json* SceneDocument::findInAdded(const std::string& guid) {
  if (!doc_.contains("objects") || !doc_["objects"].is_array()) return nullptr;
  for (ordered_json& o : doc_["objects"])
    if (ordered_json* r = searchAdded(o, guid)) return r;
  return nullptr;
}

ordered_json* SceneDocument::findInstanceJson(const std::string& rootGuid) { return findJson(rootGuid); }

void SceneDocument::fixMemberRemap(std::map<std::string, std::string>& remap) const {
  for (auto& kv : remap) {
    const MemberInfo* m = memberInfo(kv.first);
    if (!m || m->isRoot || m->added) continue;
    auto it = remap.find(m->rootGuid);
    if (it != remap.end()) kv.second = instanceObjectGuid(it->second, m->fileId);
  }
}

// ---------------------------------------------------------------- base values (the prefab without this instance's changes)

const std::map<std::string, ordered_json>& SceneDocument::baseObjects(const std::string& rootGuid) const {
  auto it = baseCache_.find(rootGuid);
  if (it != baseCache_.end()) return it->second;
  std::map<std::string, ordered_json>& out = baseCache_[rootGuid];
  ordered_json* rj = const_cast<SceneDocument*>(this)->findInstanceJson(rootGuid);
  if (!rj || !lib_) return out;
  ordered_json copy = *rj;
  for (const char* k : kModKeys) copy.erase(k);
  std::vector<ordered_json> objs;
  std::string err;
  if (expandInstance(copy, lib_->loader(), objs, nullptr, err)) for (ordered_json& o : objs) { const std::string g = o.value("guid", std::string()); out[g] = std::move(o); }
  return out;
}

namespace {
json propOf(const ordered_json& obj, const std::string& path) {
  if (path == "@name") return json(obj.value("name", std::string()));
  if (path == "@active") return json(obj.value("active", true));
  if (path == "@tag") return json(obj.value("tag", std::string()));
  if (path == "@layer") return json(obj.value("layer", 0));
  if (path.size() > 1 && obj.contains("transform") && obj["transform"].contains(path.substr(1))) return toPlain(obj["transform"][path.substr(1)]);
  return json();
}
}  // namespace

json SceneDocument::prefabValue(const std::string& guid, int comp, const std::string& field) const {
  const MemberInfo* m = memberInfo(guid);
  if (!m || (!m->isRoot && m->added)) return json();
  const auto& base = baseObjects(m->isRoot ? guid : m->rootGuid);
  auto it = base.find(guid);
  if (it == base.end()) return json();
  if (comp < 0) return propOf(it->second, "@" + field);
  if (comp >= static_cast<int>(m->comps.size())) return json();
  const MemberComp& mc = m->comps[static_cast<size_t>(comp)];
  if (!mc.addedId.empty() || !it->second.contains("components")) return json();
  int seen = 0;
  for (const ordered_json& cj : it->second["components"])
    if (cj.value("type", std::string()) == mc.type && seen++ == mc.occurrence)
      return cj.contains("fields") && cj["fields"].contains(field) ? toPlain(cj["fields"][field]) : json();
  return json();
}

// ---------------------------------------------------------------- queries

bool SceneDocument::isOverridden(const std::string& guid, int comp, const std::string& field) const {
  const MemberInfo* m = memberInfo(guid);
  if (!m || (!m->isRoot && m->added)) return false;
  ordered_json* rj = const_cast<SceneDocument*>(this)->findInstanceJson(m->rootGuid);
  if (!rj || !rj->contains("overrides")) return false;
  std::string path;
  int occ = 0;
  if (comp < 0) path = "@" + field;
  else {
    if (comp >= static_cast<int>(m->comps.size())) return false;
    path = m->comps[static_cast<size_t>(comp)].type + "." + field;
    occ = m->comps[static_cast<size_t>(comp)].occurrence;
  }
  for (const ordered_json& e : (*rj)["overrides"]) if (overrideMatches(e, m->fileId, path, occ, true)) return true;
  return false;
}

bool SceneDocument::hasObjectOverrides(const std::string& guid) const {
  const MemberInfo* m = memberInfo(guid);
  if (!m || (!m->isRoot && m->added)) return false;
  ordered_json* rj = const_cast<SceneDocument*>(this)->findInstanceJson(m->rootGuid);
  if (!rj) return false;
  for (const char* k : {"overrides", "addedComponents", "removedComponents"})
    if (rj->contains(k)) for (const ordered_json& e : (*rj)[k]) if (fid(e) == m->fileId) return true;
  return false;
}

int SceneDocument::overrideCount(const std::string& rootGuid) const {
  ordered_json* rj = const_cast<SceneDocument*>(this)->findInstanceJson(rootGuid);
  if (!rj) return 0;
  int n = 0;
  for (const char* k : kModKeys) if (rj->contains(k) && (*rj)[k].is_array()) n += static_cast<int>((*rj)[k].size());
  return n;
}

// ---------------------------------------------------------------- recording edits on members

bool SceneDocument::setMemberField(const MemberInfo& m, const std::string& guid, int component, const std::string& field, const json& value) {
  const SceneObject* so = find(guid);
  if (!so || component < 0 || component >= static_cast<int>(m.comps.size()) || component >= static_cast<int>(so->components.size())) return false;
  const MemberComp mc = m.comps[static_cast<size_t>(component)];
  ordered_json* rj = findInstanceJson(m.rootGuid);
  if (!rj) return false;
  const json& curFields = so->components[static_cast<size_t>(component)].fields;
  if (curFields.is_object() && curFields.contains(field) && curFields[field] == value) return true;
  const ordered_json nv = toOrdered(value);
  if (!mc.addedId.empty()) {  // a component this instance added: its fields live in the entry
    if (!rj->contains("addedComponents")) return false;
    for (ordered_json& e : (*rj)["addedComponents"])
      if (fid(e) == m.fileId && fid(e, "componentId") == mc.addedId) {
        pushUndo("field:" + guid + ":" + std::to_string(component) + ":" + field);
        if (!e.contains("fields") || !e["fields"].is_object()) e["fields"] = ordered_json::object();
        e["fields"][field] = nv;
        changed();
        return true;
      }
    return false;
  }
  const json base = prefabValue(guid, component, field);
  pushUndo("field:" + guid + ":" + std::to_string(component) + ":" + field);
  const std::string path = mc.type + "." + field;
  if (!base.is_null() && base == value) eraseOverrides(*rj, m.fileId, path, mc.occurrence, true);  // back to the prefab's value
  else {
    eraseOverrides(*rj, m.fileId, path, mc.occurrence, true);  // a whole-field value replaces part overrides
    setOverride(*rj, m.fileId, path, mc.occurrence, nv);
  }
  changed();
  return true;
}

bool SceneDocument::setMemberProp(const MemberInfo& m, const std::string& guid, const std::string& path, const json& value) {
  const SceneObject* so = find(guid);
  if (!so) return false;
  json cur;
  if (path == "@name") cur = so->name;
  else if (path == "@active") cur = so->active;
  else if (path == "@tag") cur = so->tag;
  else if (path == "@layer") cur = so->layer;
  else if (path == "@position") cur = arrN(so->position, 3);
  else if (path == "@rotation") cur = arrN(so->rotation, 4);
  else if (path == "@scale") cur = arrN(so->scale, 3);
  if (cur == value) return true;
  ordered_json* rj = findInstanceJson(m.rootGuid);
  if (!rj) return false;
  const json base = prefabValue(guid, -1, path.substr(1));
  pushUndo("prop:" + guid + ":" + path);
  if (!base.is_null() && base == value) eraseOverrides(*rj, m.fileId, path, 0, false);
  else setOverride(*rj, m.fileId, path, 0, toOrdered(value));
  changed();
  return true;
}

bool SceneDocument::addMemberComponent(const MemberInfo& m, const std::string& type, const json& fields) {
  ordered_json* rj = findInstanceJson(m.rootGuid);
  if (!rj) return false;
  long long next = 1;
  if (rj->contains("addedComponents"))
    for (const ordered_json& e : (*rj)["addedComponents"]) {
      const FileId c = fid(e, "componentId");
      if (!c.empty() && std::isdigit(static_cast<unsigned char>(c[0]))) next = std::max(next, std::stoll(c) + 1);
    }
  pushUndo();
  if (!rj->contains("addedComponents") || !(*rj)["addedComponents"].is_array()) (*rj)["addedComponents"] = ordered_json::array();
  ordered_json e = ordered_json::object();
  e["fileId"] = toOrdered(fileIdToJson(m.fileId));
  e["componentId"] = next;
  e["type"] = type;
  e["fields"] = toOrdered(fields);
  (*rj)["addedComponents"].push_back(std::move(e));
  changed();
  return true;
}

bool SceneDocument::removeMemberComponent(const MemberInfo& m, int component) {
  if (component < 0 || component >= static_cast<int>(m.comps.size())) return false;
  const MemberComp mc = m.comps[static_cast<size_t>(component)];
  ordered_json* rj = findInstanceJson(m.rootGuid);
  if (!rj) return false;
  pushUndo();
  if (!mc.addedId.empty()) {
    ordered_json kept = ordered_json::array();
    for (ordered_json& e : (*rj)["addedComponents"])
      if (!(fid(e) == m.fileId && fid(e, "componentId") == mc.addedId)) kept.push_back(std::move(e));
    (*rj)["addedComponents"] = std::move(kept);
    tidy(*rj);
  } else {
    // overrides of the component go with it
    if (rj->contains("overrides")) {
      ordered_json kept = ordered_json::array();
      for (ordered_json& e : (*rj)["overrides"]) {
        std::string t, rest;
        const bool mine = fid(e) == m.fileId && splitOverridePath(e.value("path", std::string()), t, rest) && t == mc.type && e.value("occurrence", 0) == mc.occurrence;
        if (!mine) kept.push_back(std::move(e));
      }
      (*rj)["overrides"] = std::move(kept);
    }
    if (!rj->contains("removedComponents") || !(*rj)["removedComponents"].is_array()) (*rj)["removedComponents"] = ordered_json::array();
    ordered_json e = ordered_json::object();
    e["fileId"] = toOrdered(fileIdToJson(m.fileId));
    e["type"] = mc.type;
    if (mc.occurrence != 0) e["occurrence"] = mc.occurrence;
    (*rj)["removedComponents"].push_back(std::move(e));
    tidy(*rj);
  }
  changed();
  return true;
}

bool SceneDocument::removeInstanceParts(const std::vector<std::string>& guids) {
  for (const std::string& g : guids) {
    const MemberInfo* mp = memberInfo(g);
    if (!mp) return false;
    const MemberInfo m = *mp;
    std::vector<std::string> sub;
    collectSubtree(g, sub);
    auto eraseEntries = [&](ordered_json* owner) {
      if (!owner || !owner->contains("addedObjects")) return;
      ordered_json kept = ordered_json::array();
      for (ordered_json& e : (*owner)["addedObjects"])
        if (std::find(sub.begin(), sub.end(), e.value("guid", std::string())) == sub.end()) kept.push_back(std::move(e));
      (*owner)["addedObjects"] = std::move(kept);
      tidy(*owner);
    };
    if (m.added) {
      eraseEntries(findInstanceJson(m.isRoot ? m.outerGuid : m.rootGuid));
      continue;
    }
    ordered_json* rj = findInstanceJson(m.rootGuid);
    if (!rj) return false;
    eraseEntries(rj);
    // overrides and component edits of the removed object go with it
    for (const char* k : {"overrides", "addedComponents", "removedComponents"}) {
      if (!rj->contains(k)) continue;
      ordered_json kept = ordered_json::array();
      for (ordered_json& e : (*rj)[k]) if (fid(e) != m.fileId) kept.push_back(std::move(e));
      (*rj)[k] = std::move(kept);
    }
    if (!rj->contains("removedObjects") || !(*rj)["removedObjects"].is_array()) (*rj)["removedObjects"] = ordered_json::array();
    (*rj)["removedObjects"].push_back(toOrdered(fileIdToJson(m.fileId)));
    tidy(*rj);
  }
  return true;
}

std::string SceneDocument::createAddedObject(const std::string& name, const MemberInfo& parent, const std::string& parentGuid,
                                             const std::vector<SceneComponent>& components) {
  ordered_json* rj = findInstanceJson(parent.rootGuid);
  if (!rj) return "";
  pushUndo();
  const std::string guid = generateGuid();
  ordered_json o = ordered_json::object();
  o["guid"] = guid;
  o["name"] = name;
  o["parent"] = parentGuid;
  o["active"] = true;
  ordered_json t = ordered_json::object();
  t["position"] = ordered_json::array({0, 0, 0});
  t["rotation"] = ordered_json::array({0, 0, 0, 1});
  t["scale"] = ordered_json::array({1, 1, 1});
  o["transform"] = std::move(t);
  ordered_json comps = ordered_json::array();
  for (const SceneComponent& c : components) {
    ordered_json cj = ordered_json::object();
    cj["type"] = c.type;
    cj["fields"] = toOrdered(c.fields);
    comps.push_back(std::move(cj));
  }
  o["components"] = std::move(comps);
  if (!rj->contains("addedObjects") || !(*rj)["addedObjects"].is_array()) (*rj)["addedObjects"] = ordered_json::array();
  (*rj)["addedObjects"].push_back(std::move(o));
  changed();
  return guid;
}

// ---------------------------------------------------------------- apply / revert

bool SceneDocument::modsFor(const std::string& guid, int comp, const std::string& field, bool wholeObject, PrefabModifications& out) const {
  out = PrefabModifications();
  const MemberInfo* m = memberInfo(guid);
  if (!m || (!m->isRoot && m->added)) return false;
  ordered_json* rj = const_cast<SceneDocument*>(this)->findInstanceJson(m->rootGuid);
  if (!rj) return false;
  PrefabInstance inst;
  std::string err;
  if (!readInstanceKeys(toPlain(*rj), inst, err)) return false;
  if (wholeObject) {
    for (const PrefabOverride& o : inst.mods.overrides) if (o.fileId == m->fileId) out.overrides.push_back(o);
    for (const PrefabAddedComponent& c : inst.mods.addedComponents) if (c.fileId == m->fileId) out.addedComponents.push_back(c);
    for (const PrefabRemovedComponent& c : inst.mods.removedComponents) if (c.fileId == m->fileId) out.removedComponents.push_back(c);
  } else {
    std::string path;
    int occ = 0;
    if (comp < 0) path = "@" + field;
    else {
      if (comp >= static_cast<int>(m->comps.size())) return false;
      path = m->comps[static_cast<size_t>(comp)].type + "." + field;
      occ = m->comps[static_cast<size_t>(comp)].occurrence;
    }
    for (const PrefabOverride& o : inst.mods.overrides) {
      PrefabOverride probe = o;
      ordered_json e = ordered_json::object();
      e["fileId"] = toOrdered(fileIdToJson(o.fileId));
      e["path"] = o.path;
      e["occurrence"] = o.occurrence;
      if (overrideMatches(e, m->fileId, path, occ, true)) out.overrides.push_back(probe);
    }
  }
  return !out.empty();
}

bool SceneDocument::revertMods(const std::string& rootGuid, const PrefabModifications& part) {
  ordered_json* rj = findInstanceJson(rootGuid);
  if (!rj || part.empty()) return false;
  PrefabInstance inst;
  std::string err;
  if (!readInstanceKeys(toPlain(*rj), inst, err)) return false;
  subtractMods(inst.mods, part);
  pushUndo();
  for (const char* k : kModKeys) rj->erase(k);
  writeInstanceKeys(*rj, inst);
  changed();
  return true;
}

bool SceneDocument::applyMods(const std::string& rootGuid, const PrefabModifications& part, const char* undoName) {
  if (!lib_ || part.empty()) return false;
  ordered_json* rj = findInstanceJson(rootGuid);
  if (!rj) return false;
  PrefabInstance inst;
  std::string err;
  if (!readInstanceKeys(toPlain(*rj), inst, err)) return false;
  PrefabAsset a;
  if (!lib_->get(inst.asset, a, err)) return false;
  std::map<std::string, FileId> ids;
  for (const auto& kv : members_) if (kv.second.rootGuid == rootGuid && !kv.second.added) ids[kv.first] = kv.second.fileId;
  std::vector<std::string> warnings;
  applyModsToPrefab(a, part, ids, warnings);
  stack_.beginGroup(undoName);
  const std::string before = lib_->readText(inst.asset);
  if (!lib_->save(inst.asset, a, err)) { stack_.endGroup(); return false; }
  stack_.record(std::make_unique<PrefabFileCommand>(lib_, inst.asset, before, lib_->readText(inst.asset)));
  subtractMods(inst.mods, part);
  pushUndo();
  for (const char* k : kModKeys) rj->erase(k);
  writeInstanceKeys(*rj, inst);
  changed();
  stack_.endGroup();
  return true;
}

bool SceneDocument::revertField(const std::string& guid, int comp, const std::string& field) {
  PrefabModifications part;
  const MemberInfo* m = memberInfo(guid);
  if (mirror_ || !m || !modsFor(guid, comp, field, false, part)) return false;
  return revertMods(m->isRoot ? guid : m->rootGuid, part);
}
bool SceneDocument::applyField(const std::string& guid, int comp, const std::string& field) {
  PrefabModifications part;
  const MemberInfo* m = memberInfo(guid);
  if (mirror_ || !m || !modsFor(guid, comp, field, false, part)) return false;
  return applyMods(m->isRoot ? guid : m->rootGuid, part, "Apply to prefab");
}
bool SceneDocument::revertObject(const std::string& guid) {
  PrefabModifications part;
  const MemberInfo* m = memberInfo(guid);
  if (mirror_ || !m || !modsFor(guid, -1, "", true, part)) return false;
  return revertMods(m->isRoot ? guid : m->rootGuid, part);
}
bool SceneDocument::applyObject(const std::string& guid) {
  PrefabModifications part;
  const MemberInfo* m = memberInfo(guid);
  if (mirror_ || !m || !modsFor(guid, -1, "", true, part)) return false;
  return applyMods(m->isRoot ? guid : m->rootGuid, part, "Apply object to prefab");
}
bool SceneDocument::revertInstance(const std::string& rootGuid) {
  ordered_json* rj = findInstanceJson(rootGuid);
  const MemberInfo* m = memberInfo(rootGuid);
  if (mirror_ || !rj || !m || !m->isRoot) return false;
  PrefabInstance inst;
  std::string err;
  if (!readInstanceKeys(toPlain(*rj), inst, err) || inst.mods.empty()) return false;
  return revertMods(rootGuid, inst.mods);
}
bool SceneDocument::applyInstance(const std::string& rootGuid) {
  ordered_json* rj = findInstanceJson(rootGuid);
  const MemberInfo* m = memberInfo(rootGuid);
  if (mirror_ || !rj || !m || !m->isRoot) return false;
  PrefabInstance inst;
  std::string err;
  if (!readInstanceKeys(toPlain(*rj), inst, err) || inst.mods.empty()) return false;
  return applyMods(rootGuid, inst.mods, "Apply all to prefab");
}

// ---------------------------------------------------------------- create / instantiate / unpack

std::string SceneDocument::createPrefab(const std::string& guid, const fs::path& folder, std::string& err) {
  if (mirror_) { err = "stop play mode first"; return ""; }
  if (!lib_ || !lib_->isOpen() || !lib_->database()) { err = "no project is open"; return ""; }
  const size_t ri = jsonIndex(guid);
  if (ri == static_cast<size_t>(-1)) { err = "select the top object of the prefab (an object inside a prefab instance cannot start a new prefab)"; return ""; }
  const ordered_json rootRaw = doc_["objects"][ri];
  AssetDatabase& db = *lib_->database();
  std::string baseName = rootRaw.value("name", std::string("Prefab"));
  for (char& c : baseName) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
  if (!AssetDatabase::validName(baseName)) baseName = "Prefab";
  fs::path created;
  if (!createAssetStub(db, AssetKind::Prefab, folder, baseName, created, err)) return "";
  const AssetEntry* ent = db.findPath(created);
  if (!ent) { err = "the new prefab was not registered"; return ""; }
  const std::string pg = ent->guid;
  auto fail = [&](const std::string& why) {
    err = why;
    std::string e2;
    db.remove(created, e2);
    return std::string();
  };
  PrefabAsset a;
  a.guid = pg;
  const bool makeVariant = rootRaw.contains("prefab") && !rootRaw["prefab"].is_null();
  std::map<std::string, FileId> idOf;
  std::vector<ordered_json> newObjects;  // the replacement of the subtree in the document
  std::set<std::string> subtreeGuids;
  if (makeVariant) {
    PrefabInstance inst;
    if (!readInstanceKeys(toPlain(rootRaw), inst, err)) return fail(err);
    a.variantBase = inst.asset;
    a.nextFileId = 1;
    std::map<std::string, FileId> ids;
    for (const auto& kv : members_) if (kv.second.rootGuid == guid && !kv.second.added) ids[kv.first] = kv.second.fileId;
    std::vector<std::string> warn;
    applyModsToPrefab(a, inst.mods, ids, warn);
    std::string cyc = lib_->wouldCycle(pg, inst.asset);
    if (!cyc.empty()) return fail(cyc);
    ordered_json nr = rootRaw;
    for (const char* k : kModKeys) nr.erase(k);
    PrefabInstance ni;
    ni.asset = pg;
    ni.fileId = "0";
    writeInstanceKeys(nr, ni);
    newObjects.push_back(std::move(nr));
    subtreeGuids.insert(guid);
  } else {
    std::vector<ordered_json> subtree;
    std::vector<std::string> queue{guid};
    subtreeGuids.insert(guid);
    // pre-order over the raw objects (file order among siblings)
    auto visit = [&](auto&& self, const std::string& g) -> void {
      for (const ordered_json& o : doc_["objects"]) {
        if (o.value("guid", std::string()) != g) continue;
        subtree.push_back(o);
        for (const ordered_json& k : doc_["objects"])
          if (k.contains("parent") && k["parent"].is_string() && k["parent"].get<std::string>() == g && subtreeGuids.insert(k.value("guid", std::string())).second)
            self(self, k.value("guid", std::string()));
        return;
      }
    };
    visit(visit, guid);
    if (!prefabFromSceneObjects(subtree, pg, a, idOf, err)) return fail(err);
    for (const PrefabObject& po : a.objects)  // a prefab cannot contain itself through a nested instance
      if (!po.nestedAsset.empty()) { std::string cyc = lib_->wouldCycle(pg, po.nestedAsset); if (!cyc.empty()) return fail(cyc); }
    ordered_json nr = makeInstanceRoot(guid, rootRaw.value("name", std::string()), rootRaw.contains("parent") && rootRaw["parent"].is_string() ? rootRaw["parent"].get<std::string>() : std::string(), a, pg);
    nr["active"] = rootRaw.value("active", true);
    if (rootRaw.contains("transform")) nr["transform"] = rootRaw["transform"];  // the instance keeps the placement it had
    newObjects.push_back(std::move(nr));
  }
  if (!lib_->save(pg, a, err)) return fail(err);
  pushUndo();
  ordered_json objs = ordered_json::array();
  for (ordered_json& o : doc_["objects"]) {
    const std::string g = o.value("guid", std::string());
    if (g == guid) { for (ordered_json& n : newObjects) objs.push_back(n); continue; }
    if (subtreeGuids.count(g)) continue;
    objs.push_back(std::move(o));
  }
  std::map<std::string, std::string> remap;  // references to the old children now point at the instance's members
  for (const auto& kv : idOf) if (kv.first != guid) remap[kv.first] = instanceObjectGuid(guid, kv.second);
  if (!remap.empty()) for (ordered_json& o : objs) if (o.value("guid", std::string()) != guid) remapAll(o, remap);
  doc_["objects"] = std::move(objs);
  changed();
  select(guid);
  return pg;
}

std::string SceneDocument::instantiatePrefab(const std::string& prefabGuid, const std::string& parentGuid, const float* position) {
  if (mirror_ || !loaded_) return "";
  if (!lib_ || !lib_->isOpen()) return "";
  PrefabAsset a;
  std::string err;
  if (!lib_->get(prefabGuid, a, err)) return "";
  if (!parentGuid.empty() && indexOf(parentGuid) < 0) return "";
  const std::string g = generateGuid();
  ordered_json o = makeInstanceRoot(g, lib_->nameOf(prefabGuid), parentGuid, a, prefabGuid);
  if (position) {
    o["transform"]["position"] = ordered_json::array();
    for (int i = 0; i < 3; ++i) o["transform"]["position"].push_back(quant(position[i]));
  }
  pushUndo();
  const MemberInfo* pm = memberInfo(parentGuid);
  if (pm && (pm->added || !pm->isRoot)) {
    ordered_json* rj = findInstanceJson(pm->rootGuid);
    if (!rj) { hasBefore_ = false; return ""; }
    if (!rj->contains("addedObjects") || !(*rj)["addedObjects"].is_array()) (*rj)["addedObjects"] = ordered_json::array();
    (*rj)["addedObjects"].push_back(std::move(o));
  } else {
    if (!doc_.contains("objects") || !doc_["objects"].is_array()) doc_["objects"] = ordered_json::array();
    doc_["objects"].push_back(std::move(o));
  }
  changed();
  select(g);
  return g;
}

bool SceneDocument::unpackPrefab(const std::string& rootGuid) {
  const MemberInfo* mp = memberInfo(rootGuid);
  if (mirror_ || !mp || !mp->isRoot || !lib_) return false;
  const MemberInfo m = *mp;
  ordered_json* rj = findInstanceJson(rootGuid);
  if (!rj) return false;
  std::vector<ordered_json> out;
  std::string err;
  if (!expandInstance(*rj, lib_->loader(), out, nullptr, err) || out.empty()) return false;
  ordered_json* container = nullptr;
  if (m.added) {
    ordered_json* outer = findInstanceJson(m.outerGuid);
    if (outer && outer->contains("addedObjects")) container = &(*outer)["addedObjects"];
  } else {
    container = &doc_["objects"];
  }
  if (!container || !container->is_array()) return false;
  size_t idx = static_cast<size_t>(-1);
  for (size_t i = 0; i < container->size(); ++i) if ((*container)[i].value("guid", std::string()) == rootGuid) idx = i;
  if (idx == static_cast<size_t>(-1)) return false;
  pushUndo();
  container->erase(idx);
  for (size_t i = 0; i < out.size(); ++i) container->insert(container->begin() + static_cast<long>(idx + i), std::move(out[i]));
  changed();
  return true;
}

}  // namespace editor
}  // namespace rensga
