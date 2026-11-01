#include "rensga/editor/input_actions_doc.h"

#include <algorithm>
#include <fstream>
#include <set>

#include "rensga/cooker/input_cook.h"

namespace rensga {
namespace editor {

using nlohmann::json;
namespace fs = std::filesystem;

const std::vector<InputChoice>& inputButtonSources() {
  static const std::vector<InputChoice> v = {{"south", "Cross (south)"}, {"east", "Circle (east)"}, {"west", "Square (west)"}, {"north", "Triangle (north)"},
                                             {"l", "L1"}, {"r", "R1"}, {"l2", "L2"}, {"r2", "R2"}, {"start", "Start"}, {"select", "Select"},
                                             {"up", "D-pad Up"}, {"down", "D-pad Down"}, {"left", "D-pad Left"}, {"right", "D-pad Right"}};
  return v;
}
const std::vector<InputChoice>& inputAxis2Sources() {
  static const std::vector<InputChoice> v = {{"nub", "Left stick"}, {"dpad", "D-pad"}, {"rightstick", "Right stick"}, {"composite", "Keys / buttons (composite)"}};
  return v;
}
const std::vector<InputChoice>& inputAxisSources() {
  static const std::vector<InputChoice> v = {{"leftx", "Left stick X"}, {"lefty", "Left stick Y"}, {"rightx", "Right stick X"}, {"righty", "Right stick Y"},
                                             {"l2", "L2 trigger"}, {"r2", "R2 trigger"}, {"composite", "Keys / buttons (composite)"}};
  return v;
}
const std::vector<std::string>& inputKeyNames() {
  static const std::vector<std::string> v = [] {
    std::vector<std::string> k;
    const char* const* n = input_cook_detail::keyNames();
    for (unsigned i = 1; i < static_cast<unsigned>(KeyCode::Count); ++i) k.push_back(n[i]);
    return k;
  }();
  return v;
}
std::string inputBindingLabel(const std::string& id) {
  if (id.size() > 4 && (id.compare(0, 4, "key:") == 0 || id.compare(0, 4, "Key:") == 0)) return "Key " + id.substr(4);
  for (const InputChoice& c : inputButtonSources()) if (id == c.id) return c.label;
  for (const InputChoice& c : inputAxis2Sources()) if (id == c.id) return c.label;
  for (const InputChoice& c : inputAxisSources()) if (id == c.id) return c.label;
  return id;
}
const char* inputTouchNote() {
  return "Touch: on a phone or tablet the on-screen controls send these same pad buttons and sticks, so every pad binding already works by touch.";
}

namespace {
void putList(json& o, const char* key, const std::vector<std::string>& v) {
  if (v.empty()) return;
  if (v.size() == 1) o[key] = v[0];
  else o[key] = v;
}
void getList(const json& o, const char* key, std::vector<std::string>& v) {
  v.clear();
  auto it = o.find(key);
  if (it == o.end()) return;
  if (it->is_string()) v.push_back(it->get<std::string>());
  else if (it->is_array()) for (const json& e : *it) if (e.is_string()) v.push_back(e.get<std::string>());
}
}  // namespace

json InputActionsDoc::toJson() const {
  json j = extra.is_object() ? extra : json::object();
  j["format"] = 1;
  j["stickDeadZone"] = stickDeadZone;
  j["stickOuterClamp"] = stickOuterClamp;
  json arr = json::array();
  for (const InputActionEntry& a : actions) {
    json o = a.extra.is_object() ? a.extra : json::object();
    o["name"] = a.name;
    o["kind"] = a.kind;
    o["source"] = a.source;
    putList(o, "alt", a.alt);
    putList(o, "up", a.up); putList(o, "down", a.down); putList(o, "left", a.left); putList(o, "right", a.right);
    putList(o, "positive", a.positive); putList(o, "negative", a.negative);
    if (a.gravity != 3) o["gravity"] = a.gravity;
    if (a.sensitivity != 3) o["sensitivity"] = a.sensitivity;
    if (a.dead != 0) o["dead"] = a.dead;
    if (a.snap) o["snap"] = true;
    if (a.invert) o["invert"] = true;
    arr.push_back(o);
  }
  j["actions"] = arr;
  return j;
}

bool InputActionsDoc::fromJson(const json& j, std::string& err) {
  if (!j.is_object()) { err = "InputActions.json must be an object"; return false; }
  InputActionsDoc d;
  d.stickDeadZone = j.value("stickDeadZone", 0.2);
  d.stickOuterClamp = j.value("stickOuterClamp", 0.95);
  const json* acts = j.contains("actions") ? &j["actions"] : nullptr;
  if (acts && !acts->is_array()) { err = "'actions' must be an array"; return false; }
  if (acts) for (const json& o : *acts) {
    if (!o.is_object()) { err = "every action must be an object"; return false; }
    InputActionEntry a;
    a.name = o.value("name", std::string());
    a.kind = o.value("kind", std::string("button"));
    a.source = o.value("source", std::string(a.kind == "axis" ? "composite" : "south"));
    getList(o, "alt", a.alt);
    getList(o, "up", a.up); getList(o, "down", a.down); getList(o, "left", a.left); getList(o, "right", a.right);
    getList(o, "positive", a.positive); getList(o, "negative", a.negative);
    a.gravity = o.value("gravity", 3.0);
    a.sensitivity = o.value("sensitivity", 3.0);
    a.dead = o.value("dead", 0.0);
    a.snap = o.value("snap", false);
    a.invert = o.value("invert", false);
    for (auto it = o.begin(); it != o.end(); ++it) {
      static const std::set<std::string> known = {"name", "kind", "source", "alt", "up", "down", "left", "right", "positive", "negative",
                                                  "gravity", "sensitivity", "dead", "snap", "invert"};
      if (!known.count(it.key())) a.extra[it.key()] = it.value();
    }
    d.actions.push_back(std::move(a));
  }
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != "format" && it.key() != "stickDeadZone" && it.key() != "stickOuterClamp" && it.key() != "actions") d.extra[it.key()] = it.value();
  *this = std::move(d);
  return true;
}

std::vector<std::string> InputActionsDoc::validate() const {
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const InputActionEntry& a : actions)
    if (!seen.insert(a.name).second) out.push_back("action '" + a.name + "': the name is used twice");
  CookedInput ci;
  std::vector<std::string> errs;
  cookInputActions(toJson(), [](const std::string&) { return 0u; }, ci, errs);
  for (std::string& e : errs) out.push_back(std::move(e));
  return out;
}

int InputActionsDoc::indexOf(const std::string& name) const {
  for (size_t i = 0; i < actions.size(); ++i) if (actions[i].name == name) return static_cast<int>(i);
  return -1;
}

int InputActionsDoc::add(const std::string& kind) {
  InputActionEntry a;
  a.kind = kind == "axis" || kind == "axis2" ? kind : "button";
  a.source = a.kind == "axis" ? "leftx" : (a.kind == "axis2" ? "nub" : "south");
  a.name = "Action";
  for (int n = 2; indexOf(a.name) >= 0; ++n) a.name = "Action " + std::to_string(n);
  actions.push_back(std::move(a));
  return static_cast<int>(actions.size()) - 1;
}

bool InputActionsDoc::remove(int index) {
  if (index < 0 || index >= static_cast<int>(actions.size())) return false;
  actions.erase(actions.begin() + index);
  return true;
}

bool InputActionsDoc::move(int from, int to) {
  const int n = static_cast<int>(actions.size());
  if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
  InputActionEntry a = std::move(actions[static_cast<size_t>(from)]);
  actions.erase(actions.begin() + from);
  actions.insert(actions.begin() + to, std::move(a));
  return true;
}

bool InputActionsDoc::rename(int index, const std::string& name) {
  if (index < 0 || index >= static_cast<int>(actions.size()) || name.empty() || name.size() >= 24) return false;
  const int other = indexOf(name);
  if (other >= 0 && other != index) return false;
  actions[static_cast<size_t>(index)].name = name;
  return true;
}

bool InputActionsDoc::setKind(int index, const std::string& kind) {
  if (index < 0 || index >= static_cast<int>(actions.size())) return false;
  if (kind != "button" && kind != "axis" && kind != "axis2") return false;
  InputActionEntry& a = actions[static_cast<size_t>(index)];
  if (a.kind == kind) return true;
  a.kind = kind;
  a.source = kind == "axis" ? "leftx" : (kind == "axis2" ? "nub" : "south");
  a.alt.clear(); a.up.clear(); a.down.clear(); a.left.clear(); a.right.clear(); a.positive.clear(); a.negative.clear();
  return true;
}

bool loadInputActions(const fs::path& root, InputActionsDoc& out, std::string& err) {
  const fs::path f = root / "ProjectSettings" / "InputActions.json";
  std::error_code ec;
  if (!fs::exists(f, ec)) { out = InputActionsDoc(); return true; }
  std::ifstream in(f);
  json j = json::parse(in, nullptr, false);
  if (j.is_discarded()) { err = f.string() + ": not valid JSON"; return false; }
  return out.fromJson(j, err);
}

bool saveInputActions(const fs::path& root, const InputActionsDoc& d, std::string& err) {
  const fs::path dir = root / "ProjectSettings", f = dir / "InputActions.json", tmp = dir / "InputActions.json.tmp";
  std::error_code ec;
  fs::create_directories(dir, ec);
  {
    std::ofstream o(tmp, std::ios::binary);
    if (!o) { err = "cannot write " + tmp.string(); return false; }
    o << d.toJson().dump(2) << "\n";
  }
  fs::rename(tmp, f, ec);
  if (ec) { err = ec.message(); return false; }
  return true;
}

}  // namespace editor
}  // namespace rensga
