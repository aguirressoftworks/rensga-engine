#include "rensga/editor/anim_clip_doc.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "rensga/cooker/anim_cook.h"
#include "rensga/core/reflect.h"

namespace rensga {
namespace editor {

using nlohmann::json;

namespace {
constexpr size_t kUndoLimit = 200;

struct PropInfo {
  bool ok = false;
  FieldType type = FieldType::Scalar;
  bool transform = false;
};

PropInfo propInfo(const std::string& property) {
  PropInfo p;
  const size_t dot = property.find('.');
  if (dot == std::string::npos) return p;
  const std::string comp = property.substr(0, dot), rest = property.substr(dot + 1);
  if (comp == "Transform") {
    const std::string field = rest.substr(0, rest.find('.'));
    p.ok = true;
    p.transform = true;
    p.type = field == "localRotation" ? FieldType::Quaternion : FieldType::Vector3;
    if (field != "localPosition" && field != "localRotation" && field != "localScale" && field != "localEulerAngles") p.ok = false;
    return p;
  }
  if (comp == "GameObject") {
    p.ok = rest == "active";
    p.type = FieldType::Bool;
    return p;
  }
  const TypeInfo* ti = TypeRegistry::instance().find(comp.c_str());
  FieldPath fp;
  if (!ti || !parseFieldPath(*ti, rest.c_str(), fp) || !fp.field) return p;
  p.ok = true;
  p.type = fp.field->type;
  return p;
}
}  // namespace

AnimValueKind animValueKind(const std::string& property) {
  const PropInfo p = propInfo(property);
  if (!p.ok) return AnimValueKind::Unknown;
  switch (p.type) {
    case FieldType::I32: return AnimValueKind::Int;
    case FieldType::Bool: return AnimValueKind::Bool;
    case FieldType::Enum: return AnimValueKind::Enum;
    case FieldType::Color32: return AnimValueKind::Color;
    case FieldType::Guid: return AnimValueKind::Guid;
    default: return AnimValueKind::Float;
  }
}

std::vector<AnimComponentProps> animatableProperties(const std::vector<std::string>& types) {
  std::vector<AnimComponentProps> out;
  AnimComponentProps t;
  t.component = "Transform";
  for (const char* v : {"x", "y", "z"}) t.properties.push_back({std::string("Transform.localPosition.") + v, std::string("Position.") + v});
  for (const char* v : {"x", "y", "z"}) t.properties.push_back({std::string("Transform.localEulerAngles.") + v, std::string("Rotation (euler).") + v});
  for (const char* v : {"x", "y", "z"}) t.properties.push_back({std::string("Transform.localScale.") + v, std::string("Scale.") + v});
  out.push_back(t);
  AnimComponentProps g;
  g.component = "GameObject";
  g.properties.push_back({"GameObject.active", "Active"});
  out.push_back(g);
  for (const std::string& name : types) {
    const TypeInfo* ti = TypeRegistry::instance().find(name.c_str());
    if (!ti || name == "Transform") continue;
    AnimComponentProps cp;
    cp.component = ti->name;
    for (u32 i = 0; i < ti->fieldCount; ++i) {
      const FieldDesc& f = ti->fields[i];
      auto add = [&](const char* part, const char* label) {
        std::string prop = std::string(ti->name) + "." + f.name + (part ? std::string(".") + part : std::string());
        cp.properties.push_back({prop, std::string(f.name) + (label ? std::string(".") + label : std::string())});
      };
      switch (f.type) {
        case FieldType::Scalar: case FieldType::I32: case FieldType::Bool: case FieldType::Enum: case FieldType::Guid: add(nullptr, nullptr); break;
        case FieldType::Vector2: add("x", "x"); add("y", "y"); break;
        case FieldType::Vector3: add("x", "x"); add("y", "y"); add("z", "z"); break;
        case FieldType::Quaternion: add("x", "x"); add("y", "y"); add("z", "z"); add("w", "w"); break;
        case FieldType::Color32: add("r", "r"); add("g", "g"); add("b", "b"); add("a", "a"); break;
        default: break;
      }
    }
    if (!cp.properties.empty()) out.push_back(cp);
  }
  return out;
}

// ---- document ------------------------------------------------------------------------------------------------------------------

json AnimClipDoc::toJson() const {
  json j = {{"format", 1}, {"kind", "PropertyClip"}, {"name", name_}, {"fps", fps_}, {"length", length_}, {"loop", loop_}};
  json cs = json::array();
  for (const AnimCurve& c : curves_) {
    json cj = {{"property", c.property}, {"interp", c.interp}};
    if (!c.target.empty()) cj["target"] = c.target;
    json ks = json::array();
    const bool isGuid = animValueKind(c.property) == AnimValueKind::Guid;
    for (const AnimKey& k : c.keys) {
      json kj = {{"t", k.tick}};
      if (isGuid) kj["v"] = k.guid;
      else if (animValueKind(c.property) == AnimValueKind::Bool) kj["v"] = k.value >= 0.5;
      else kj["v"] = k.value;
      if (k.inTangent != 0 || k.outTangent != 0) { kj["in"] = k.inTangent; kj["out"] = k.outTangent; }
      ks.push_back(kj);
    }
    cj["keys"] = ks;
    cs.push_back(cj);
  }
  j["curves"] = cs;
  json es = json::array();
  for (const AnimEventDef& e : events_) {
    json ej = {{"t", e.tick}, {"function", e.function}};
    if (e.intValue) ej["int"] = e.intValue;
    if (e.floatValue != 0) ej["float"] = e.floatValue;
    if (!e.stringValue.empty()) ej["string"] = e.stringValue;
    es.push_back(ej);
  }
  j["events"] = es;
  return j;
}

bool AnimClipDoc::fromJson(const json& j, std::string& err) {
  if (!j.is_object() || j.value("format", 0) != 1) { err = "not a property clip (format 1)"; return false; }
  name_ = j.value("name", std::string());
  fps_ = j.value("fps", 30u);
  length_ = j.value("length", 30u);
  loop_ = j.value("loop", false);
  if (fps_ == 0 || fps_ > 240) { err = "\"fps\" must be 1..240"; return false; }
  curves_.clear();
  events_.clear();
  for (const json& cj : j.value("curves", json::array())) {
    AnimCurve c;
    c.target = cj.value("target", std::string());
    c.property = cj.value("property", std::string());
    c.interp = cj.value("interp", std::string("linear"));
    const bool isGuid = animValueKind(c.property) == AnimValueKind::Guid;
    for (const json& kj : cj.value("keys", json::array())) {
      AnimKey k;
      k.tick = kj.value("t", 0u);
      if (isGuid) k.guid = kj.value("v", std::string());
      else if (kj.contains("v") && kj["v"].is_boolean()) k.value = kj["v"].get<bool>() ? 1 : 0;
      else if (kj.contains("v") && kj["v"].is_number()) k.value = kj["v"].get<double>();
      k.inTangent = kj.value("in", 0.0);
      k.outTangent = kj.value("out", 0.0);
      c.keys.push_back(k);
    }
    curves_.push_back(std::move(c));
  }
  for (const json& ej : j.value("events", json::array())) {
    AnimEventDef e;
    e.tick = ej.value("t", 0u);
    e.function = ej.value("function", std::string());
    e.intValue = ej.value("int", 0);
    e.floatValue = ej.value("float", 0.0);
    e.stringValue = ej.value("string", std::string());
    events_.push_back(e);
  }
  for (size_t i = 0; i < curves_.size(); ++i) sort(static_cast<int>(i));
  std::stable_sort(events_.begin(), events_.end(), [](const AnimEventDef& a, const AnimEventDef& b) { return a.tick < b.tick; });
  return true;
}

bool AnimClipDoc::load(const std::filesystem::path& file, std::string& err) {
  std::ifstream in(file, std::ios::binary);
  if (!in) { err = "cannot open " + file.string(); return false; }
  std::ostringstream ss;
  ss << in.rdbuf();
  json j = json::parse(ss.str(), nullptr, false);
  if (j.is_discarded()) { err = file.string() + ": invalid JSON"; return false; }
  if (!fromJson(j, err)) return false;
  if (name_.empty()) name_ = file.filename().string();
  path_ = file;
  loaded_ = true;
  dirty_ = false;
  undo_.clear();
  redo_.clear();
  return true;
}

bool AnimClipDoc::save(std::string& err) {
  if (!loaded_) { err = "no clip loaded"; return false; }
  std::ofstream f(path_, std::ios::binary | std::ios::trunc);
  if (!f) { err = "cannot write " + path_.string(); return false; }
  f << toJson().dump(1) << "\n";
  f.flush();
  if (!f) { err = "write failed for " + path_.string(); return false; }
  dirty_ = false;
  return true;
}

void AnimClipDoc::push() {
  undo_.push_back(toJson());
  if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
  redo_.clear();
}

bool AnimClipDoc::undo() {
  if (undo_.empty()) return false;
  redo_.push_back(toJson());
  json j = undo_.back();
  undo_.pop_back();
  std::string err;
  const bool ok = fromJson(j, err);
  dirty_ = true;
  return ok;
}
bool AnimClipDoc::redo() {
  if (redo_.empty()) return false;
  undo_.push_back(toJson());
  json j = redo_.back();
  redo_.pop_back();
  std::string err;
  const bool ok = fromJson(j, err);
  dirty_ = true;
  return ok;
}

void AnimClipDoc::setFps(u32 fps) { if (fps == 0 || fps > 240 || fps == fps_) return; push(); fps_ = fps; touch(); }
void AnimClipDoc::setLength(u32 ticks) { if (ticks == length_) return; push(); length_ = ticks; touch(); }
void AnimClipDoc::setLoop(bool loop) { if (loop == loop_) return; push(); loop_ = loop; touch(); }

int AnimClipDoc::findCurve(const std::string& target, const std::string& property) const {
  for (size_t i = 0; i < curves_.size(); ++i)
    if (curves_[i].target == target && curves_[i].property == property) return static_cast<int>(i);
  return -1;
}
int AnimClipDoc::addCurve(const std::string& target, const std::string& property) {
  const int e = findCurve(target, property);
  if (e >= 0) return e;
  if (!propInfo(property).ok) return -1;
  push();
  AnimCurve c;
  c.target = target;
  c.property = property;
  const AnimValueKind k = animValueKind(property);
  c.interp = (k == AnimValueKind::Float || k == AnimValueKind::Color) ? "linear" : "step";
  curves_.push_back(c);
  touch();
  return static_cast<int>(curves_.size() - 1);
}
bool AnimClipDoc::removeCurve(int curve) {
  if (curve < 0 || curve >= static_cast<int>(curves_.size())) return false;
  push();
  curves_.erase(curves_.begin() + curve);
  touch();
  return true;
}
bool AnimClipDoc::setInterp(int curve, const std::string& interp) {
  if (curve < 0 || curve >= static_cast<int>(curves_.size()) || (interp != "linear" && interp != "step" && interp != "smooth")) return false;
  if (curves_[curve].interp == interp) return true;
  push();
  curves_[curve].interp = interp;
  touch();
  return true;
}
void AnimClipDoc::sort(int curve) {
  std::stable_sort(curves_[curve].keys.begin(), curves_[curve].keys.end(), [](const AnimKey& a, const AnimKey& b) { return a.tick < b.tick; });
}
bool AnimClipDoc::hasKeyAt(int curve, u32 tick) const {
  if (curve < 0 || curve >= static_cast<int>(curves_.size())) return false;
  for (const AnimKey& k : curves_[curve].keys) if (k.tick == tick) return true;
  return false;
}
bool AnimClipDoc::setKey(int curve, u32 tick, double value) {
  if (curve < 0 || curve >= static_cast<int>(curves_.size()) || tick > 100000) return false;
  push();
  AnimCurve& c = curves_[curve];
  bool found = false;
  for (AnimKey& k : c.keys) if (k.tick == tick) { k.value = value; found = true; }
  if (!found) { AnimKey k; k.tick = tick; k.value = value; c.keys.push_back(k); sort(curve); }
  fixLength(tick);
  touch();
  return true;
}
bool AnimClipDoc::setKeyGuid(int curve, u32 tick, const std::string& guid) {
  if (curve < 0 || curve >= static_cast<int>(curves_.size()) || tick > 100000) return false;
  push();
  AnimCurve& c = curves_[curve];
  bool found = false;
  for (AnimKey& k : c.keys) if (k.tick == tick) { k.guid = guid; found = true; }
  if (!found) { AnimKey k; k.tick = tick; k.guid = guid; c.keys.push_back(k); sort(curve); }
  fixLength(tick);
  touch();
  return true;
}
bool AnimClipDoc::setTangents(int curve, int key, double in, double out) {
  if (curve < 0 || curve >= static_cast<int>(curves_.size()) || key < 0 || key >= static_cast<int>(curves_[curve].keys.size())) return false;
  push();
  curves_[curve].keys[key].inTangent = in;
  curves_[curve].keys[key].outTangent = out;
  touch();
  return true;
}
bool AnimClipDoc::moveKey(int curve, int key, u32 newTick) {
  if (curve < 0 || curve >= static_cast<int>(curves_.size()) || key < 0 || key >= static_cast<int>(curves_[curve].keys.size()) || newTick > 100000) return false;
  AnimCurve& c = curves_[curve];
  if (c.keys[key].tick == newTick) return true;
  for (const AnimKey& k : c.keys) if (k.tick == newTick) return false;
  push();
  c.keys[key].tick = newTick;
  sort(curve);
  fixLength(newTick);
  touch();
  return true;
}
bool AnimClipDoc::removeKey(int curve, int key) {
  if (curve < 0 || curve >= static_cast<int>(curves_.size()) || key < 0 || key >= static_cast<int>(curves_[curve].keys.size())) return false;
  push();
  curves_[curve].keys.erase(curves_[curve].keys.begin() + key);
  touch();
  return true;
}
int AnimClipDoc::addEvent(u32 tick, const std::string& function, int i, double f, const std::string& s) {
  if (function.empty() || tick > 100000) return -1;
  push();
  AnimEventDef e;
  e.tick = tick;
  e.function = function;
  e.intValue = i;
  e.floatValue = f;
  e.stringValue = s;
  events_.push_back(e);
  std::stable_sort(events_.begin(), events_.end(), [](const AnimEventDef& a, const AnimEventDef& b) { return a.tick < b.tick; });
  fixLength(tick);
  touch();
  for (size_t k = 0; k < events_.size(); ++k) if (events_[k].tick == tick && events_[k].function == function) return static_cast<int>(k);
  return 0;
}
bool AnimClipDoc::removeEvent(int event) {
  if (event < 0 || event >= static_cast<int>(events_.size())) return false;
  push();
  events_.erase(events_.begin() + event);
  touch();
  return true;
}
bool AnimClipDoc::setEvent(int event, const AnimEventDef& e) {
  if (event < 0 || event >= static_cast<int>(events_.size()) || e.function.empty() || e.tick > 100000) return false;
  push();
  events_[event] = e;
  std::stable_sort(events_.begin(), events_.end(), [](const AnimEventDef& a, const AnimEventDef& b) { return a.tick < b.tick; });
  fixLength(e.tick);
  touch();
  return true;
}
double AnimClipDoc::evalCurve(const AnimCurve& c, double t) {
  const size_t n = c.keys.size();
  if (n == 0) return 0;
  const AnimValueKind kind = animValueKind(c.property);
  const bool step = c.interp == "step" || kind == AnimValueKind::Int || kind == AnimValueKind::Bool || kind == AnimValueKind::Enum || kind == AnimValueKind::Guid;
  if (n == 1 || t <= c.keys.front().tick) return c.keys.front().value;
  if (t >= c.keys.back().tick) return c.keys.back().value;
  size_t lo = 0;
  while (lo + 2 < n && c.keys[lo + 1].tick <= t) ++lo;
  const AnimKey& a = c.keys[lo];
  const AnimKey& b = c.keys[lo + 1];
  if (step) return a.value;
  const double dt = static_cast<double>(b.tick) - a.tick;
  if (dt <= 0) return b.value;
  const double s = (t - a.tick) / dt;
  if (c.interp == "smooth") {
    const double s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * a.value + (s3 - 2 * s2 + s) * dt * a.outTangent + (-2 * s3 + 3 * s2) * b.value + (s3 - s2) * dt * b.inTangent;
  }
  return a.value + (b.value - a.value) * s;
}

bool AnimClipDoc::moveEvent(int event, u32 newTick) {
  if (event < 0 || event >= static_cast<int>(events_.size()) || newTick > 100000) return false;
  push();
  events_[event].tick = newTick;
  std::stable_sort(events_.begin(), events_.end(), [](const AnimEventDef& a, const AnimEventDef& b) { return a.tick < b.tick; });
  fixLength(newTick);
  touch();
  return true;
}

bool AnimClipDoc::bake(BakedClip& out, std::string& err) const {
  out = BakedClip();
  std::vector<std::string> strings;
  std::vector<char>& strs = out.strs;
  strs.push_back(0);
  auto addStr = [&](const std::string& s) {
    if (s.empty()) return 0u;
    const u32 off = static_cast<u32>(strs.size());
    strs.insert(strs.end(), s.begin(), s.end());
    strs.push_back(0);
    return off;
  };
  if (!cooker::bakePropClip(toJson(), Guid(), addStr, out.blob, out.rec, err)) return false;
  if (out.blob.empty()) out.blob.assign(16, 0);
  return true;
}

double AnimClipDoc::valueAt(int curve, double ticks) const {
  BakedClip b;
  std::string err;
  if (curve < 0 || curve >= static_cast<int>(curves_.size()) || !bake(b, err)) return 0;
  // Baking keeps the order of curves except for euler channels, which expand: look the curve up by property instead.
  const PropClipSource s = b.source();
  const AnimCurve& c = curves_[curve];
  const std::string comp = c.property.substr(0, c.property.find('.'));
  const std::string rest = c.property.substr(c.property.find('.') + 1);
  const std::string field = rest.substr(0, rest.find('.'));
  for (u32 i = 0; i < s.clip->curveCount; ++i) {
    const PackPropCurve& pc = s.curves()[i];
    if (pc.componentType != fnv1a32(comp.c_str()) || pc.fieldHash != fnv1a32(field.c_str()) || s.str(pc.pathStr) != c.target) continue;
    const size_t dot = rest.find('.');
    int part = -1;
    if (dot != std::string::npos) { const char ch = rest[dot + 1]; part = ch == 'x' || ch == 'r' ? 0 : ch == 'y' || ch == 'g' ? 1 : ch == 'z' || ch == 'b' ? 2 : 3; }
    if (pc.part != part) continue;
    return PropClips::sampleCurve(s, pc, static_cast<u64>(ticks * 65536.0));
  }
  return 0;
}

}  // namespace editor
}  // namespace rensga
