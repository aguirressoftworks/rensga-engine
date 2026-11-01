#include "rensga/editor/anim_controller_doc.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "rensga/cooker/anim_cook.h"

namespace rensga {
namespace editor {

using nlohmann::json;

namespace {
const char* typeName(AnimParamType t) {
  switch (t) {
    case AnimParamType::Float: return "float";
    case AnimParamType::Int: return "int";
    case AnimParamType::Bool: return "bool";
    default: return "trigger";
  }
}
AnimParamType typeFrom(const std::string& s) {
  return s == "int" ? AnimParamType::Int : (s == "bool" ? AnimParamType::Bool : (s == "trigger" ? AnimParamType::Trigger : AnimParamType::Float));
}
constexpr size_t kUndoLimit = 200;
}  // namespace

int AnimControllerDoc::findState(const std::string& n) const {
  for (size_t i = 0; i < states_.size(); ++i) if (states_[i].name == n) return static_cast<int>(i);
  return -1;
}
int AnimControllerDoc::findParam(const std::string& n) const {
  for (size_t i = 0; i < params_.size(); ++i) if (params_[i].name == n) return static_cast<int>(i);
  return -1;
}

json AnimControllerDoc::toJson() const {
  json j = {{"format", 1}, {"kind", "AnimatorController"}, {"name", name_}};
  json ps = json::array();
  for (const AnimParamDef& p : params_) {
    json pj = {{"name", p.name}, {"type", typeName(p.type)}};
    if (p.type == AnimParamType::Float) pj["default"] = p.defaultValue;
    else if (p.type == AnimParamType::Int) pj["default"] = static_cast<long long>(p.defaultValue);
    else if (p.type == AnimParamType::Bool) pj["default"] = p.defaultValue != 0;
    ps.push_back(pj);
  }
  j["parameters"] = ps;
  j["defaultState"] = default_;
  json ss = json::array();
  for (const AnimStateDef& s : states_) {
    json sj = {{"name", s.name}, {"motion", s.motion}, {"loop", s.loop}, {"speed", s.speed}, {"pos", json::array({s.x, s.y})}};
    if (!s.speedParameter.empty()) sj["speedParameter"] = s.speedParameter;
    ss.push_back(sj);
  }
  j["states"] = ss;
  json ts = json::array();
  for (const AnimTransDef& t : trans_) {
    json tj = {{"from", t.from}, {"to", t.to}, {"hasExitTime", t.hasExitTime}, {"exitTime", t.exitTime}, {"duration", t.duration}};
    json cs = json::array();
    for (const AnimCondDef& c : t.conditions) {
      json cj = {{"param", c.param}, {"mode", c.mode}};
      if (c.mode != "if" && c.mode != "ifNot") cj["value"] = c.value;
      cs.push_back(cj);
    }
    tj["conditions"] = cs;
    ts.push_back(tj);
  }
  j["transitions"] = ts;
  return j;
}

bool AnimControllerDoc::fromJson(const json& j, std::string& err) {
  if (!j.is_object() || j.value("format", 0) != 1) { err = "not an animator controller (format 1)"; return false; }
  name_ = j.value("name", std::string());
  default_ = j.value("defaultState", std::string());
  params_.clear();
  states_.clear();
  trans_.clear();
  for (const json& pj : j.value("parameters", json::array())) {
    AnimParamDef p;
    p.name = pj.value("name", std::string());
    p.type = typeFrom(pj.value("type", std::string("float")));
    if (pj.contains("default")) p.defaultValue = pj["default"].is_boolean() ? (pj["default"].get<bool>() ? 1 : 0) : pj["default"].get<double>();
    params_.push_back(p);
  }
  int idx = 0;
  for (const json& sj : j.value("states", json::array())) {
    AnimStateDef s;
    s.name = sj.value("name", std::string());
    s.motion = sj.value("motion", std::string());
    s.loop = sj.value("loop", true);
    s.speed = sj.value("speed", 1.0);
    s.speedParameter = sj.value("speedParameter", std::string());
    if (sj.contains("pos") && sj["pos"].is_array() && sj["pos"].size() == 2) {
      s.x = sj["pos"][0].get<float>();
      s.y = sj["pos"][1].get<float>();
    } else {
      s.x = 60.0f + 200.0f * static_cast<float>(idx % 3);  // laid out when the file has no positions
      s.y = 60.0f + 120.0f * static_cast<float>(idx / 3);
    }
    states_.push_back(s);
    ++idx;
  }
  for (const json& tj : j.value("transitions", json::array())) {
    AnimTransDef t;
    t.from = tj.value("from", std::string());
    t.to = tj.value("to", std::string());
    t.hasExitTime = tj.value("hasExitTime", false);
    t.exitTime = tj.value("exitTime", 1.0);
    t.duration = tj.value("duration", 0.0);
    for (const json& cj : tj.value("conditions", json::array())) {
      AnimCondDef c;
      c.param = cj.value("param", std::string());
      c.mode = cj.value("mode", std::string("if"));
      c.value = cj.value("value", 0.0);
      t.conditions.push_back(c);
    }
    trans_.push_back(t);
  }
  return true;
}

bool AnimControllerDoc::load(const std::filesystem::path& file, std::string& err) {
  std::ifstream in(file, std::ios::binary);
  if (!in) { err = "cannot open " + file.string(); return false; }
  std::ostringstream ss;
  ss << in.rdbuf();
  json j = json::parse(ss.str(), nullptr, false);
  if (j.is_discarded()) { err = file.string() + ": invalid JSON"; return false; }
  if (!fromJson(j, err)) return false;
  path_ = file;
  loaded_ = true;
  dirty_ = false;
  undo_.clear();
  redo_.clear();
  return true;
}

bool AnimControllerDoc::save(std::string& err) {
  if (!loaded_) { err = "no controller loaded"; return false; }
  std::ofstream f(path_, std::ios::binary | std::ios::trunc);
  if (!f) { err = "cannot write " + path_.string(); return false; }
  f << toJson().dump(1) << "\n";
  f.flush();
  if (!f) { err = "write failed for " + path_.string(); return false; }
  dirty_ = false;
  return true;
}

void AnimControllerDoc::push() {
  undo_.push_back(toJson());
  if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
  redo_.clear();
}
bool AnimControllerDoc::undo() {
  if (undo_.empty()) return false;
  redo_.push_back(toJson());
  json j = undo_.back();
  undo_.pop_back();
  std::string err;
  const bool ok = fromJson(j, err);
  dirty_ = true;
  return ok;
}
bool AnimControllerDoc::redo() {
  if (redo_.empty()) return false;
  undo_.push_back(toJson());
  json j = redo_.back();
  redo_.pop_back();
  std::string err;
  const bool ok = fromJson(j, err);
  dirty_ = true;
  return ok;
}

int AnimControllerDoc::addState(const std::string& name, float x, float y) {
  if (name.empty() || findState(name) >= 0) return -1;
  push();
  AnimStateDef s;
  s.name = name;
  s.x = x;
  s.y = y;
  states_.push_back(s);
  if (default_.empty()) default_ = name;
  touch();
  return static_cast<int>(states_.size() - 1);
}
bool AnimControllerDoc::removeState(const std::string& name) {
  const int i = findState(name);
  if (i < 0) return false;
  push();
  states_.erase(states_.begin() + i);
  trans_.erase(std::remove_if(trans_.begin(), trans_.end(), [&](const AnimTransDef& t) { return t.from == name || t.to == name; }), trans_.end());
  if (default_ == name) default_ = states_.empty() ? std::string() : states_[0].name;
  touch();
  return true;
}
bool AnimControllerDoc::renameState(const std::string& from, const std::string& to) {
  const int i = findState(from);
  if (i < 0 || to.empty() || findState(to) >= 0) return false;
  push();
  states_[i].name = to;
  for (AnimTransDef& t : trans_) { if (t.from == from) t.from = to; if (t.to == from) t.to = to; }
  if (default_ == from) default_ = to;
  touch();
  return true;
}
bool AnimControllerDoc::setDefaultState(const std::string& name) {
  if (findState(name) < 0) return false;
  if (default_ == name) return true;
  push();
  default_ = name;
  touch();
  return true;
}
bool AnimControllerDoc::setMotion(const std::string& state, const std::string& guid) {
  const int i = findState(state);
  if (i < 0) return false;
  push();
  states_[i].motion = guid;
  touch();
  return true;
}
bool AnimControllerDoc::setStateOptions(const std::string& state, bool loop, double speed, const std::string& sp) {
  const int i = findState(state);
  if (i < 0 || speed < 0) return false;
  if (!sp.empty() && (findParam(sp) < 0 || params_[findParam(sp)].type != AnimParamType::Float)) return false;
  push();
  states_[i].loop = loop;
  states_[i].speed = speed;
  states_[i].speedParameter = sp;
  touch();
  return true;
}
bool AnimControllerDoc::moveState(const std::string& name, float x, float y) {
  const int i = findState(name);
  if (i < 0) return false;
  states_[i].x = x;
  states_[i].y = y;
  touch();
  return true;
}
int AnimControllerDoc::addParam(const std::string& name, AnimParamType type, double def) {
  if (name.empty() || findParam(name) >= 0 || params_.size() >= 16) return -1;
  push();
  AnimParamDef p;
  p.name = name;
  p.type = type;
  p.defaultValue = def;
  params_.push_back(p);
  touch();
  return static_cast<int>(params_.size() - 1);
}
bool AnimControllerDoc::removeParam(const std::string& name) {
  const int i = findParam(name);
  if (i < 0) return false;
  push();
  params_.erase(params_.begin() + i);
  for (AnimTransDef& t : trans_)
    t.conditions.erase(std::remove_if(t.conditions.begin(), t.conditions.end(), [&](const AnimCondDef& c) { return c.param == name; }), t.conditions.end());
  for (AnimStateDef& s : states_) if (s.speedParameter == name) s.speedParameter.clear();
  touch();
  return true;
}
bool AnimControllerDoc::setParamDefault(const std::string& name, double v) {
  const int i = findParam(name);
  if (i < 0) return false;
  push();
  params_[i].defaultValue = v;
  touch();
  return true;
}
int AnimControllerDoc::addTransition(const std::string& from, const std::string& to) {
  if ((from != "*" && findState(from) < 0) || findState(to) < 0) return -1;
  push();
  AnimTransDef t;
  t.from = from;
  t.to = to;
  trans_.push_back(t);
  touch();
  return static_cast<int>(trans_.size() - 1);
}
bool AnimControllerDoc::removeTransition(int index) {
  if (index < 0 || index >= static_cast<int>(trans_.size())) return false;
  push();
  trans_.erase(trans_.begin() + index);
  touch();
  return true;
}
bool AnimControllerDoc::setTransition(int index, bool hasExit, double exitTime, double duration) {
  if (index < 0 || index >= static_cast<int>(trans_.size()) || duration < 0 || duration > 60 || exitTime < 0) return false;
  push();
  trans_[index].hasExitTime = hasExit;
  trans_[index].exitTime = exitTime;
  trans_[index].duration = duration;
  touch();
  return true;
}
bool AnimControllerDoc::addCondition(int transition, const AnimCondDef& c) {
  if (transition < 0 || transition >= static_cast<int>(trans_.size()) || findParam(c.param) < 0) return false;
  push();
  trans_[transition].conditions.push_back(c);
  touch();
  return true;
}
bool AnimControllerDoc::removeCondition(int transition, int cond) {
  if (transition < 0 || transition >= static_cast<int>(trans_.size()) || cond < 0 || cond >= static_cast<int>(trans_[transition].conditions.size())) return false;
  push();
  trans_[transition].conditions.erase(trans_[transition].conditions.begin() + cond);
  touch();
  return true;
}
bool AnimControllerDoc::setCondition(int transition, int cond, const AnimCondDef& c) {
  if (transition < 0 || transition >= static_cast<int>(trans_.size()) || cond < 0 || cond >= static_cast<int>(trans_[transition].conditions.size()) || findParam(c.param) < 0) return false;
  push();
  trans_[transition].conditions[cond] = c;
  touch();
  return true;
}

std::string AnimControllerDoc::validate() const {
  cooker::MotionResolver mr;
  mr.resolve = [](const std::string&, PackAnimStateKind& kind, Guid& out, std::string&) {
    kind = PackAnimStateKind::PropClip;
    out = Guid();
    return true;
  };
  std::vector<u8> blob;
  PackAnimController c;
  std::string err;
  if (!cooker::bakeController(toJson(), Guid(), 60, mr, [](const std::string&) { return 0u; }, blob, c, err)) return err;
  return std::string();
}

}  // namespace editor
}  // namespace rensga
