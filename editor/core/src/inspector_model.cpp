#include "rensga/editor/inspector_model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>

#include "rensga/core/object.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/reflect_method.h"
#include "rensga/editor/editor_prefs.h"
#include "rensga/editor/game_module.h"

namespace rensga {
namespace editor {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// A temporary, detached instance of a reflected type (aligned buffer, constructed through the TypeInfo).
struct TempInstance {
  const TypeInfo& ti;
  void* p = nullptr;
  explicit TempInstance(const TypeInfo& t) : ti(t) {
    p = ::operator new(ti.size, std::align_val_t(ti.align ? ti.align : 1));
    ti.construct(p);
  }
  ~TempInstance() {
    if (ti.destruct) ti.destruct(p);
    ::operator delete(p, std::align_val_t(ti.align ? ti.align : 1));
  }
  TempInstance(const TempInstance&) = delete;
  TempInstance& operator=(const TempInstance&) = delete;
};

// Floats written by a callback are shortened to what a float round-trips through, like the inspector widgets do.
void tidy(json& j) {
  if (j.is_number_float()) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.7g", j.get<double>());
    j = std::strtod(buf, nullptr);
  } else if (j.is_array() || j.is_object()) {
    for (auto& v : j) tidy(v);
  }
}

json overlay(json base, const json& over) {
  if (over.is_object())
    for (auto it = over.begin(); it != over.end(); ++it) base[it.key()] = it.value();
  return base;
}

const TypeInfo* typeOf(const std::string& n) { return TypeRegistry::instance().find(n.c_str()); }

int indexOfType(const SceneObject& o, const std::string& type) {
  for (size_t i = 0; i < o.components.size(); ++i)
    if (o.components[i].type == type) return static_cast<int>(i);
  return -1;
}

void collectRequirements(const TypeInfo& ti, const SceneObject& o, std::vector<std::string>& out, int depth) {
  if (depth > 8) return;
  for (u32 i = 0; i < ti.requireCount; ++i) {
    const std::string r = ti.requireNames[i];
    if (indexOfType(o, r) >= 0 || std::find(out.begin(), out.end(), r) != out.end()) continue;
    if (const TypeInfo* rt = typeOf(r)) collectRequirements(*rt, o, out, depth + 1);
    if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
  }
}

const SceneComponent* componentAt(const SceneDocument& d, const ComponentRef& r) {
  const SceneObject* o = d.find(r.guid);
  if (!o || r.index < 0 || r.index >= static_cast<int>(o->components.size())) return nullptr;
  return &o->components[r.index];
}

OpResult fail(std::string why) {
  OpResult r;
  r.reason = std::move(why);
  return r;
}
OpResult good() {
  OpResult r;
  r.ok = true;
  return r;
}

struct Gesture {  // groups every edit of one operation into a single undo step
  SceneDocument& d;
  explicit Gesture(SceneDocument& doc) : d(doc) { d.beginGesture(); }
  ~Gesture() { d.endGesture(); }
};

json validated(const SceneComponent& c) {
  const TypeInfo* ti = typeOf(c.type);
  return ti ? runEditorCallback(*ti, EditorCallback::Validate, c.fields) : c.fields;
}

}  // namespace

// ---- reflection helpers ---------------------------------------------------------------------------------------

json typeDefaults(const TypeInfo& ti) {
  if (!ti.construct) return json::object();
  TempInstance t(ti);
  return fieldsToJson(ti, t.p);
}

std::string prettyFieldName(const char* n) {
  std::string out;
  for (const char* p = n; *p; ++p) {
    if (p == n) out += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    else {
      if (std::isupper(static_cast<unsigned char>(*p)) && !std::isupper(static_cast<unsigned char>(p[-1]))) out += ' ';
      out += *p;
    }
  }
  return out;
}

json runEditorCallback(const TypeInfo& ti, EditorCallback cb, const json& fields, bool* ran) {
  if (ran) *ran = false;
  if (!ti.construct || !ti.asBehaviour) return fields;
  TempInstance t(ti);
  json merged = overlay(fieldsToJson(ti, t.p), fields);
  std::string err;
  if (!fieldsFromJson(ti, t.p, merged, &err)) return fields;  // stored data does not fit the type: leave it alone
  const json before = fieldsToJson(ti, t.p);
  Behaviour* b = ti.asBehaviour(t.p);
  if (ran) *ran = true;
  if (cb == EditorCallback::Validate) b->OnValidate();
  else b->Reset();
  const json after = fieldsToJson(ti, t.p);
  json out = fields.is_object() ? fields : json::object();
  for (u32 i = 0; i < ti.fieldCount; ++i) {
    const char* n = ti.fields[i].name;
    if (after[n] != before[n]) {
      out[n] = after[n];
      tidy(out[n]);
    }
  }
  return out;
}

json runContextMethod(const TypeInfo& ti, const std::string& method, const json& fields) {
  const MethodInfo* m = findMethod(ti, method.c_str());
  if (!m || !m->invoke || m->arg != MethodArgKind::None || !ti.construct) return fields;
  TempInstance t(ti);
  json merged = overlay(fieldsToJson(ti, t.p), fields);
  std::string err;
  if (!fieldsFromJson(ti, t.p, merged, &err)) return fields;
  const json before = fieldsToJson(ti, t.p);
  m->invoke(t.p, MethodArg());
  const json after = fieldsToJson(ti, t.p);
  json out = fields.is_object() ? fields : json::object();
  for (u32 i = 0; i < ti.fieldCount; ++i) {
    const char* n = ti.fields[i].name;
    if (after[n] != before[n]) {
      out[n] = after[n];
      tidy(out[n]);
    }
  }
  return out;
}

ComponentClipboard& componentClipboard() {
  static ComponentClipboard c;
  return c;
}

// ---- Add Component search ---------------------------------------------------------------------------------------

int fuzzyScore(const std::string& query, const std::string& text) {
  if (query.empty()) return 0;
  auto low = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
  size_t pos = 0;
  int score = 0;
  bool prevHit = false;
  for (char qc : query) {
    if (qc == ' ') continue;
    size_t i = pos;
    while (i < text.size() && low(text[i]) != low(qc)) ++i;
    if (i >= text.size()) return -1;
    const bool wordStart = i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1])) ||
                           (std::isupper(static_cast<unsigned char>(text[i])) && std::islower(static_cast<unsigned char>(text[i - 1])));
    score += 1 + (prevHit && i == pos ? 5 : 0) + (wordStart ? 4 : 0) - static_cast<int>(std::min<size_t>(i - pos, 3));
    prevHit = true;
    pos = i + 1;
  }
  std::string q, t;
  for (char c : query) if (c != ' ') q += low(c);
  for (char c : text) t += low(c);
  if (t.compare(0, q.size(), q) == 0) score += 20;
  else if (t.find(q) != std::string::npos) score += 8;
  return score;
}

std::string componentCategory(const TypeInfo& ti) {
  const std::string n = ti.name;
  auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
  for (const TypeInfo* g : GameModule::instance().types())
    if (g == &ti) return "Scripts";
  if (has("Collider") || has("Rigidbody") || has("CharacterController") || has("Physics")) return "Physics";
  if (has("Camera") || has("Light") || has("Renderer") || has("Mesh") || has("Sprite") || has("Tilemap") || has("Particle")) return "Rendering";
  if (has("Audio") || has("Music") || has("Sound")) return "Audio";
  if (has("UI") || has("Canvas") || has("Button") || has("Text")) return "UI";
  if (ti.asBehaviour) return "Scripts";
  return "Other";
}

namespace {
std::vector<std::string> g_recent;
bool g_recentLoaded = false;

fs::path recentFile() {
  fs::path p = prefsFile();
  return p.empty() ? p : p.parent_path() / "recent_components.json";
}

void loadRecent() {
  if (g_recentLoaded) return;
  g_recentLoaded = true;
  const fs::path f = recentFile();
  if (f.empty()) return;
  std::ifstream in(f, std::ios::binary);
  if (!in) return;
  json j = json::parse(in, nullptr, false);
  if (j.is_array())
    for (const json& v : j)
      if (v.is_string() && g_recent.size() < 8) g_recent.push_back(v.get<std::string>());
}
}  // namespace

std::vector<std::string> recentComponents() {
  loadRecent();
  return g_recent;
}

void noteComponentUsed(const std::string& typeName) {
  loadRecent();
  g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), typeName), g_recent.end());
  g_recent.insert(g_recent.begin(), typeName);
  if (g_recent.size() > 8) g_recent.resize(8);
  const fs::path f = recentFile();
  if (f.empty()) return;
  std::error_code ec;
  fs::create_directories(f.parent_path(), ec);
  std::ofstream o(f, std::ios::binary | std::ios::trunc);
  o << json(g_recent).dump() << "\n";
}

void resetRecentComponents() {
  g_recent.clear();
  g_recentLoaded = true;
}

// ---- rules -------------------------------------------------------------------------------------------------------

std::vector<std::string> missingRequirements(const TypeInfo& ti, const SceneObject& o) {
  std::vector<std::string> out;
  collectRequirements(ti, o, out, 0);
  return out;
}

std::string greyReason(const std::string& type, const std::set<std::string>& allowed, const std::string& targetName) {
  if (allowed.empty() || allowed.count(type)) return "";
  return "Not supported by " + targetName + ": the cook will reject this component.";
}

std::string addBlockReason(const std::string& type, const SceneObject& o, const std::set<std::string>& allowed, const std::string& targetName) {
  const TypeInfo* ti = typeOf(type);
  if (!ti || !ti->asComponent) return "Unknown component type " + type;
  if (!allowed.empty() && !allowed.count(type)) return "Not in the component list of " + targetName + " (ConsoleTarget allowedComponents).";
  for (const std::string& r : missingRequirements(*ti, o))
    if (!allowed.empty() && !allowed.count(r)) return type + " requires " + r + ", which " + targetName + " does not support.";
  return "";
}

std::string removeBlockReason(const SceneObject& o, int idx) {
  if (idx < 0 || idx >= static_cast<int>(o.components.size())) return "No such component";
  const std::string type = o.components[idx].type;
  int same = 0;
  for (const SceneComponent& c : o.components) same += c.type == type;
  if (same > 1) return "";
  for (size_t j = 0; j < o.components.size(); ++j) {
    if (static_cast<int>(j) == idx) continue;
    const TypeInfo* ti = typeOf(o.components[j].type);
    if (!ti) continue;
    for (u32 k = 0; k < ti->requireCount; ++k)
      if (type == ti->requireNames[k]) return "Required by " + o.components[j].type;
  }
  return "";
}

bool isMissingScript(const std::string& type) { return typeOf(type) == nullptr; }

// ---- document operations -----------------------------------------------------------------------------------------

namespace {
bool addOne(SceneDocument& d, const std::string& guid, const std::string& type) {
  const TypeInfo* ti = typeOf(type);
  if (!ti) return false;
  return d.addComponent(guid, type, runEditorCallback(*ti, EditorCallback::Reset, typeDefaults(*ti)));
}
}  // namespace

OpResult addComponentTo(SceneDocument& d, const std::string& guid, const std::string& type, const std::set<std::string>& allowed,
                        const std::string& targetName) {
  const SceneObject* o = d.find(guid);
  if (!o) return fail("No such object");
  const TypeInfo* ti = typeOf(type);
  if (!ti) return fail("Unknown component type " + type);
  std::string why = addBlockReason(type, *o, allowed, targetName);
  if (!why.empty()) return fail(why);
  {
    Gesture g(d);
    for (const std::string& r : missingRequirements(*ti, *o))
      if (!addOne(d, guid, r)) return fail("Cannot add required " + r);
    if (!addOne(d, guid, type)) return fail("Cannot add " + type);
  }
  noteComponentUsed(type);
  return good();
}

OpResult removeComponentFrom(SceneDocument& d, const std::string& guid, int idx) {
  const SceneObject* o = d.find(guid);
  if (!o) return fail("No such object");
  std::string why = removeBlockReason(*o, idx);
  if (!why.empty()) return fail(why);
  return d.removeComponent(guid, idx) ? good() : fail("Cannot remove");
}

OpResult moveComponentBy(SceneDocument& d, const std::string& guid, int idx, int delta) {
  const SceneObject* o = d.find(guid);
  if (!o || idx < 0 || idx >= static_cast<int>(o->components.size())) return fail("No such component");
  const int to = idx + delta;
  if (to < 0) return fail("Already first");
  if (to >= static_cast<int>(o->components.size())) return fail("Already last");
  return d.moveComponent(guid, idx, to) ? good() : fail("Cannot move");
}

OpResult resetComponent(SceneDocument& d, const std::string& guid, int idx) {
  const SceneObject* o = d.find(guid);
  if (!o || idx < 0 || idx >= static_cast<int>(o->components.size())) return fail("No such component");
  const std::string type = o->components[idx].type;
  const TypeInfo* ti = typeOf(type);
  if (!ti) return fail("Missing script (" + type + "): nothing to reset to");
  return d.setComponentFields(guid, idx, runEditorCallback(*ti, EditorCallback::Reset, typeDefaults(*ti))) ? good() : fail("Cannot reset");
}

OpResult copyComponentValues(const SceneDocument& d, const std::string& guid, int idx) {
  const SceneObject* o = d.find(guid);
  if (!o || idx < 0 || idx >= static_cast<int>(o->components.size())) return fail("No such component");
  const SceneComponent& c = o->components[idx];
  ComponentClipboard& cb = componentClipboard();
  cb.has = true;
  cb.type = c.type;
  const TypeInfo* ti = typeOf(c.type);
  cb.fields = ti ? overlay(typeDefaults(*ti), c.fields) : c.fields;
  return good();
}

OpResult pasteComponentValues(SceneDocument& d, const std::string& guid, int idx) {
  const ComponentClipboard& cb = componentClipboard();
  if (!cb.has) return fail("Nothing copied");
  const SceneObject* o = d.find(guid);
  if (!o || idx < 0 || idx >= static_cast<int>(o->components.size())) return fail("No such component");
  if (o->components[idx].type != cb.type) return fail("The clipboard holds " + cb.type + " values, not " + o->components[idx].type);
  Gesture g(d);
  if (!d.setComponentFields(guid, idx, cb.fields)) return fail("Cannot paste");
  if (const SceneObject* n = d.find(guid)) {
    json v = validated(n->components[idx]);
    d.setComponentFields(guid, idx, v);
  }
  return good();
}

OpResult runContextMenu(SceneDocument& d, const std::string& guid, int idx, const std::string& method) {
  const SceneObject* o = d.find(guid);
  if (!o || idx < 0 || idx >= static_cast<int>(o->components.size())) return fail("No such component");
  const TypeInfo* ti = typeOf(o->components[idx].type);
  if (!ti) return fail("Missing script");
  const json nf = runContextMethod(*ti, method, o->components[idx].fields);
  Gesture g(d);
  if (nf != o->components[idx].fields && !d.setComponentFields(guid, idx, nf)) return fail("Cannot apply");
  if (const SceneObject* n = d.find(guid)) d.setComponentFields(guid, idx, validated(n->components[idx]));
  return good();
}

std::vector<ComponentRef> sameTypeAcross(const SceneDocument& d, const std::vector<std::string>& guids, const std::string& type) {
  std::vector<ComponentRef> refs;
  for (const std::string& g : guids) {
    const SceneObject* o = d.find(g);
    if (!o) return {};
    int i = indexOfType(*o, type);
    if (i < 0) return {};
    refs.push_back({g, i});
  }
  return refs;
}

std::vector<std::string> commonComponentTypes(const SceneDocument& d, const std::vector<std::string>& guids) {
  std::vector<std::string> out;
  if (guids.empty()) return out;
  const SceneObject* first = d.find(guids[0]);
  if (!first) return out;
  for (const SceneComponent& c : first->components) {
    if (std::find(out.begin(), out.end(), c.type) != out.end()) continue;
    bool all = true;
    for (size_t i = 1; i < guids.size() && all; ++i) {
      const SceneObject* o = d.find(guids[i]);
      all = o && indexOfType(*o, c.type) >= 0;
    }
    if (all) out.push_back(c.type);
  }
  return out;
}

json fieldValue(const SceneDocument& d, const ComponentRef& r, const std::string& field) {
  const SceneComponent* c = componentAt(d, r);
  if (!c) return json();
  if (c->fields.contains(field)) return c->fields[field];
  if (const TypeInfo* ti = typeOf(c->type)) {
    json def = typeDefaults(*ti);
    if (def.contains(field)) return def[field];
  }
  return json();
}

bool fieldMixed(const SceneDocument& d, const std::vector<ComponentRef>& refs, const std::string& field) {
  for (size_t i = 1; i < refs.size(); ++i)
    if (fieldValue(d, refs[i], field) != fieldValue(d, refs[0], field)) return true;
  return false;
}

OpResult editFields(SceneDocument& d, const std::vector<ComponentRef>& refs, const std::string& field, const json& value) {
  if (refs.empty()) return fail("Nothing selected");
  Gesture g(d);
  for (const ComponentRef& r : refs) {
    if (!componentAt(d, r)) return fail("No such component");
    if (!d.setField(r.guid, r.index, field, value)) return fail("Cannot set " + field);
    if (const SceneComponent* c = componentAt(d, r)) {
      // OnValidate side effects go back field by field (also works while playing, when edits go to the running World).
      const json v = validated(*c);
      const json cur = c->fields;
      for (auto it = v.begin(); it != v.end(); ++it)
        if (!cur.contains(it.key()) || cur[it.key()] != it.value()) d.setField(r.guid, r.index, it.key(), it.value());
    }
  }
  return good();
}

OpResult editObjects(SceneDocument& d, const std::vector<std::string>& guids, const std::string& what, const json& value) {
  if (guids.empty()) return fail("Nothing selected");
  Gesture g(d);
  for (const std::string& id : guids) {
    bool ok = false;
    if (what == "name" && value.is_string()) ok = d.rename(id, value.get<std::string>());
    else if (what == "active" && value.is_boolean()) ok = d.setActive(id, value.get<bool>());
    else if (what == "tag" && value.is_string()) ok = d.setTag(id, value.get<std::string>());
    else if (what == "layer" && value.is_number_integer()) ok = d.setLayer(id, value.get<int>());
    if (!ok) return fail("Cannot set " + what);
  }
  return good();
}

}  // namespace editor
}  // namespace rensga
