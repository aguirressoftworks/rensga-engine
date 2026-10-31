#include "rensga/editor/project_settings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr int kFormat = 1;

json extraWithout(const json& j, std::initializer_list<const char*> known) {
  json e = json::object();
  if (!j.is_object()) return e;
  for (auto it = j.begin(); it != j.end(); ++it) {
    bool k = std::string(it.key()) == "format";
    for (const char* n : known) k = k || it.key() == n;
    if (!k) e[it.key()] = it.value();
  }
  return e;
}

void mergeExtra(json& out, const json& extra) {
  if (!extra.is_object()) return;
  for (auto it = extra.begin(); it != extra.end(); ++it)
    if (!out.contains(it.key())) out[it.key()] = it.value();
}

bool checkFormat(const json& j, const char* what, std::string& err) {
  if (!j.is_object()) { err = std::string(what) + ": not a JSON object"; return false; }
  if (j.contains("format") && (!j["format"].is_number_integer() || j["format"].get<int>() != kFormat)) {
    err = std::string(what) + ": unsupported format version";
    return false;
  }
  return true;
}

bool readFile(const fs::path& p, std::string& text) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  text = ss.str();
  return true;
}

bool writeFileAtomic(const fs::path& p, const std::string& text, std::string& err) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  const fs::path tmp = p.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) { err = "cannot write " + tmp.string(); return false; }
    f << text;
    if (!f) { err = "write failed: " + tmp.string(); return false; }
  }
  fs::rename(tmp, p, ec);
  if (ec) { err = "cannot replace " + p.string() + ": " + ec.message(); fs::remove(tmp, ec); return false; }
  return true;
}

template <class T>
bool loadFile(const fs::path& root, const char* name, T& out, std::string& err) {
  const fs::path p = root / "ProjectSettings" / name;
  std::error_code ec;
  if (!fs::exists(p, ec)) { out = T::defaults(); return true; }
  std::string text;
  if (!readFile(p, text)) { err = std::string("ProjectSettings/") + name + ": cannot open"; return false; }
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded()) { err = std::string("ProjectSettings/") + name + ": invalid JSON"; return false; }
  T tmp = T::defaults();
  std::string e;
  if (!tmp.fromJson(j, e)) { err = std::string("ProjectSettings/") + name + ": " + e; return false; }
  out = std::move(tmp);
  return true;
}

template <class T>
bool saveFile(const fs::path& root, const char* name, const T& s, std::string& err) {
  const std::vector<std::string> problems = s.validate();
  if (!problems.empty()) { err = std::string("ProjectSettings/") + name + ": " + problems[0]; return false; }
  return writeFileAtomic(root / "ProjectSettings" / name, s.toJson().dump(2) + "\n", err);
}

}  // namespace

// ---------------------------------------------------------------- TagManager
TagManagerSettings TagManagerSettings::defaults() {
  TagManagerSettings s;
  s.layers[0] = "Default";
  s.layers[1] = "TransparentFX";
  s.layers[2] = "Ignore Raycast";
  s.layers[4] = "Water";
  s.layers[5] = "UI";
  return s;
}

json TagManagerSettings::toJson() const {
  json j = {{"format", kFormat}, {"tags", tags}, {"layers", json::array()}};
  for (const std::string& l : layers) j["layers"].push_back(l);
  mergeExtra(j, extra);
  return j;
}

bool TagManagerSettings::fromJson(const json& j, std::string& err) {
  if (!checkFormat(j, "TagManager", err)) return false;
  TagManagerSettings s = defaults();
  if (j.contains("tags")) {
    if (!j["tags"].is_array()) { err = "tags must be an array of strings"; return false; }
    s.tags.clear();
    for (const json& t : j["tags"]) {
      if (!t.is_string()) { err = "tags must be an array of strings"; return false; }
      s.tags.push_back(t.get<std::string>());
    }
  }
  if (j.contains("layers")) {
    if (!j["layers"].is_array() || j["layers"].size() > static_cast<size_t>(kLayerCount)) { err = "layers must be an array of at most 32 strings"; return false; }
    s.layers.fill(std::string());
    for (size_t i = 0; i < j["layers"].size(); ++i) {
      if (!j["layers"][i].is_string()) { err = "layers must be an array of strings"; return false; }
      s.layers[i] = j["layers"][i].get<std::string>();
    }
  }
  s.extra = extraWithout(j, {"tags", "layers"});
  *this = std::move(s);
  return true;
}

std::vector<std::string> TagManagerSettings::validate() const {
  std::vector<std::string> p;
  std::set<std::string> seen;
  for (const std::string& t : tags) {
    if (t.empty()) p.push_back("a tag name is empty");
    else if (t == "Untagged") p.push_back("'Untagged' is built in");
    else if (t.size() > 63) p.push_back("tag '" + t + "' is longer than 63 characters");
    else if (!seen.insert(t).second) p.push_back("duplicate tag '" + t + "'");
  }
  if (layers[0] != "Default") p.push_back("layer 0 must be named 'Default'");
  seen.clear();
  for (int i = 0; i < kLayerCount; ++i) {
    const std::string& l = layers[static_cast<size_t>(i)];
    if (l.empty()) continue;
    if (l.size() > 31) p.push_back("layer " + std::to_string(i) + " name is longer than 31 characters");
    else if (!seen.insert(l).second) p.push_back("duplicate layer name '" + l + "'");
  }
  return p;
}

bool TagManagerSettings::hasTag(const std::string& t) const { return std::find(tags.begin(), tags.end(), t) != tags.end(); }

int TagManagerSettings::layerIndex(const std::string& name) const {
  if (name.empty()) return -1;
  for (int i = 0; i < kLayerCount; ++i) if (layers[static_cast<size_t>(i)] == name) return i;
  return -1;
}

// ---------------------------------------------------------------- Physics
PhysicsSettings PhysicsSettings::defaults() { return PhysicsSettings(); }

namespace {
std::array<int, 2> normPair(int a, int b) { return a <= b ? std::array<int, 2>{a, b} : std::array<int, 2>{b, a}; }
}  // namespace

json PhysicsSettings::toJson() const {
  json ig = json::array();
  for (const auto& p : ignore) ig.push_back(json::array({p[0], p[1]}));
  json j = {{"format", kFormat}, {"gravity", json::array({gravity[0], gravity[1], gravity[2]})}, {"ignore", ig},
            {"queriesHitTriggers", queriesHitTriggers}};
  mergeExtra(j, extra);
  return j;
}

bool PhysicsSettings::fromJson(const json& j, std::string& err) {
  if (!checkFormat(j, "Physics", err)) return false;
  PhysicsSettings s;
  if (j.contains("gravity")) {
    const json& g = j["gravity"];
    if (!g.is_array() || g.size() != 3 || !g[0].is_number() || !g[1].is_number() || !g[2].is_number()) { err = "gravity must be [x,y,z]"; return false; }
    for (int i = 0; i < 3; ++i) s.gravity[i] = g[static_cast<size_t>(i)].get<float>();
  }
  if (j.contains("ignore")) {
    if (!j["ignore"].is_array()) { err = "ignore must be an array of [layerA,layerB] pairs"; return false; }
    for (const json& p : j["ignore"]) {
      if (!p.is_array() || p.size() != 2 || !p[0].is_number_integer() || !p[1].is_number_integer()) { err = "ignore must be an array of [layerA,layerB] pairs"; return false; }
      const int a = p[0].get<int>(), b = p[1].get<int>();
      if (a < 0 || b < 0 || a >= kLayerCount || b >= kLayerCount) { err = "ignore pair has a layer outside 0..31"; return false; }
      s.setCollides(a, b, false);
    }
  }
  if (j.contains("queriesHitTriggers")) {
    if (!j["queriesHitTriggers"].is_boolean()) { err = "queriesHitTriggers must be a boolean"; return false; }
    s.queriesHitTriggers = j["queriesHitTriggers"].get<bool>();
  }
  s.extra = extraWithout(j, {"gravity", "ignore", "queriesHitTriggers"});
  *this = std::move(s);
  return true;
}

std::vector<std::string> PhysicsSettings::validate() const {
  std::vector<std::string> p;
  for (int i = 0; i < 3; ++i) if (!std::isfinite(gravity[i]) || std::fabs(gravity[i]) > 1000.0f) p.push_back("gravity must be finite and within +-1000");
  for (const auto& q : ignore) if (q[0] < 0 || q[1] < 0 || q[0] >= kLayerCount || q[1] >= kLayerCount) p.push_back("ignore pair outside 0..31");
  return p;
}

bool PhysicsSettings::collides(int a, int b) const {
  const auto k = normPair(a, b);
  return std::find(ignore.begin(), ignore.end(), k) == ignore.end();
}

void PhysicsSettings::setCollides(int a, int b, bool on) {
  const auto k = normPair(a, b);
  auto it = std::lower_bound(ignore.begin(), ignore.end(), k);
  const bool present = it != ignore.end() && *it == k;
  if (on && present) ignore.erase(it);
  else if (!on && !present) ignore.insert(it, k);
}

std::array<uint32_t, kLayerCount> PhysicsSettings::collisionMasks() const {
  std::array<uint32_t, kLayerCount> m;
  m.fill(0xFFFFFFFFu);
  for (const auto& p : ignore) {
    m[static_cast<size_t>(p[0])] &= ~(1u << p[1]);
    m[static_cast<size_t>(p[1])] &= ~(1u << p[0]);
  }
  return m;
}

// ---------------------------------------------------------------- Time
TimeSettings TimeSettings::defaults() { return TimeSettings(); }

json TimeSettings::toJson() const {
  json j = {{"format", kFormat}, {"fixedTimestep", fixedTimestep}, {"maximumDelta", maximumDelta}, {"timeScale", timeScale}};
  mergeExtra(j, extra);
  return j;
}

bool TimeSettings::fromJson(const json& j, std::string& err) {
  if (!checkFormat(j, "Time", err)) return false;
  TimeSettings s;
  for (const char* k : {"fixedTimestep", "maximumDelta", "timeScale"})
    if (j.contains(k) && !j[k].is_number()) { err = std::string(k) + " must be a number"; return false; }
  s.fixedTimestep = j.value("fixedTimestep", s.fixedTimestep);
  s.maximumDelta = j.value("maximumDelta", s.maximumDelta);
  s.timeScale = j.value("timeScale", s.timeScale);
  s.extra = extraWithout(j, {"fixedTimestep", "maximumDelta", "timeScale"});
  *this = std::move(s);
  return true;
}

std::vector<std::string> TimeSettings::validate() const {
  std::vector<std::string> p;
  if (!(fixedTimestep >= 0.001 && fixedTimestep <= 0.1)) p.push_back("fixedTimestep must be between 0.001 and 0.1 seconds");
  if (!(maximumDelta >= fixedTimestep && maximumDelta <= 1.0)) p.push_back("maximumDelta must be between fixedTimestep and 1 second");
  if (!(timeScale >= 0.0 && timeScale <= 100.0)) p.push_back("timeScale must be between 0 and 100");
  return p;
}

// ---------------------------------------------------------------- Project.json
ProjectBuildSettings ProjectBuildSettings::defaults() { return ProjectBuildSettings(); }

json ProjectBuildSettings::toJson() const {
  json en = json::array(), all = json::array();
  for (const BuildScene& s : scenes) {
    all.push_back({{"guid", s.guid}, {"enabled", s.enabled}});
    if (s.enabled) en.push_back(s.guid);
  }
  json j = {{"format", kFormat}, {"name", name}, {"target", target}, {"development", development}, {"scenes", en}, {"buildScenes", all}};
  mergeExtra(j, extra);
  return j;
}

bool ProjectBuildSettings::fromJson(const json& j, std::string& err) {
  if (!checkFormat(j, "Project", err)) return false;
  ProjectBuildSettings s;
  for (const char* k : {"name", "target"})
    if (j.contains(k) && !j[k].is_string()) { err = std::string(k) + " must be a string"; return false; }
  if (j.contains("development") && !j["development"].is_boolean()) { err = "development must be a boolean"; return false; }
  s.name = j.value("name", std::string());
  s.target = j.value("target", std::string());
  s.development = j.value("development", false);
  if (j.contains("buildScenes")) {
    if (!j["buildScenes"].is_array()) { err = "buildScenes must be an array"; return false; }
    for (const json& e : j["buildScenes"]) {
      if (!e.is_object() || !e.contains("guid") || !e["guid"].is_string()) { err = "buildScenes entries need a guid string"; return false; }
      s.scenes.push_back({e["guid"].get<std::string>(), e.value("enabled", true)});
    }
  } else if (j.contains("scenes")) {
    if (!j["scenes"].is_array()) { err = "scenes must be an array of GUID strings"; return false; }
    for (const json& g : j["scenes"]) {
      if (!g.is_string()) { err = "scenes must be an array of GUID strings"; return false; }
      s.scenes.push_back({g.get<std::string>(), true});
    }
  }
  s.extra = extraWithout(j, {"name", "target", "development", "scenes", "buildScenes"});
  *this = std::move(s);
  return true;
}

std::vector<std::string> ProjectBuildSettings::validate() const {
  std::vector<std::string> p;
  if (name.empty()) p.push_back("the project needs a name");
  if (target.empty()) p.push_back("the project needs a target id");
  std::set<std::string> seen;
  for (const BuildScene& s : scenes) {
    if (s.guid.size() != 36) p.push_back("scene GUID '" + s.guid + "' is not 36 characters");
    else if (!seen.insert(s.guid).second) p.push_back("scene " + s.guid + " is listed twice");
  }
  return p;
}

std::vector<std::string> ProjectBuildSettings::enabledScenes() const {
  std::vector<std::string> r;
  for (const BuildScene& s : scenes) if (s.enabled) r.push_back(s.guid);
  return r;
}

bool ProjectBuildSettings::addScene(const std::string& guid, bool enabled) {
  for (const BuildScene& s : scenes) if (s.guid == guid) return false;
  scenes.push_back({guid, enabled});
  return true;
}

bool ProjectBuildSettings::removeScene(const std::string& guid) {
  for (size_t i = 0; i < scenes.size(); ++i)
    if (scenes[i].guid == guid) { scenes.erase(scenes.begin() + static_cast<long>(i)); return true; }
  return false;
}

bool ProjectBuildSettings::setSceneEnabled(const std::string& guid, bool enabled) {
  for (BuildScene& s : scenes) if (s.guid == guid) { s.enabled = enabled; return true; }
  return false;
}

bool ProjectBuildSettings::moveScene(int from, int to) {
  const int n = static_cast<int>(scenes.size());
  if (from < 0 || to < 0 || from >= n || to >= n) return false;
  BuildScene b = scenes[static_cast<size_t>(from)];
  scenes.erase(scenes.begin() + from);
  scenes.insert(scenes.begin() + to, b);
  return true;
}

// ---------------------------------------------------------------- files
bool loadTagManager(const fs::path& r, TagManagerSettings& o, std::string& e) { return loadFile(r, "TagManager.json", o, e); }
bool saveTagManager(const fs::path& r, const TagManagerSettings& s, std::string& e) { return saveFile(r, "TagManager.json", s, e); }
bool loadPhysics(const fs::path& r, PhysicsSettings& o, std::string& e) { return loadFile(r, "Physics.json", o, e); }
bool savePhysics(const fs::path& r, const PhysicsSettings& s, std::string& e) { return saveFile(r, "Physics.json", s, e); }
bool loadTime(const fs::path& r, TimeSettings& o, std::string& e) { return loadFile(r, "Time.json", o, e); }
bool saveTime(const fs::path& r, const TimeSettings& s, std::string& e) { return saveFile(r, "Time.json", s, e); }
bool loadProjectBuild(const fs::path& r, ProjectBuildSettings& o, std::string& e) { return loadFile(r, "Project.json", o, e); }
bool saveProjectBuild(const fs::path& r, const ProjectBuildSettings& s, std::string& e) { return saveFile(r, "Project.json", s, e); }

}  // namespace editor
}  // namespace rensga
