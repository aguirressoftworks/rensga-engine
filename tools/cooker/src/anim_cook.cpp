#include "rensga/cooker/anim_cook.h"

#include <math.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "rensga/core/prop_clip.h"
#include "rensga/core/reflect.h"

namespace rensga {
namespace cooker {

using nlohmann::json;

namespace {

constexpr u32 kMaxCurves = 64, kMaxKeys = 512, kMaxEvents = 64, kMaxParams = 16, kMaxStates = 32;

void alignBlob(std::vector<u8>& b) {
  while (b.size() % 16) b.push_back(0);
}
template <class T>
u32 putBlob(std::vector<u8>& b, const std::vector<T>& v) {
  alignBlob(b);
  const u32 off = static_cast<u32>(b.size());
  const u8* p = reinterpret_cast<const u8*>(v.data());
  b.insert(b.end(), p, p + v.size() * sizeof(T));
  return off;
}

u32 bits(Scalar f) {
  u32 b;
  memcpy(&b, &f, 4);
  return b;
}

struct Prop {
  u32 componentType = 0, fieldHash = 0;
  int part = -1;
  FieldType type = FieldType::Scalar;
  const FieldDesc* desc = nullptr;
  bool euler = false;  // Transform.localEulerAngles: baked to localRotation
};

int partOf(const std::string& s) {
  if (s == "x" || s == "r") return 0;
  if (s == "y" || s == "g") return 1;
  if (s == "z" || s == "b") return 2;
  if (s == "w" || s == "a") return 3;
  return -2;
}

bool resolveProperty(const std::string& property, Prop& p, std::string& err) {
  const size_t dot = property.find('.');
  if (dot == std::string::npos || dot == 0) { err = "property '" + property + "' must be Component.field[.part]"; return false; }
  const std::string comp = property.substr(0, dot);
  const std::string rest = property.substr(dot + 1);
  p = Prop();
  p.componentType = fnv1a32(comp.c_str());
  if (comp == "Transform") {
    const size_t d2 = rest.find('.');
    const std::string field = rest.substr(0, d2);
    const int part = d2 == std::string::npos ? -2 : partOf(rest.substr(d2 + 1));
    if (field == "localPosition" || field == "localScale") p.type = FieldType::Vector3;
    else if (field == "localRotation") p.type = FieldType::Quaternion;
    else if (field == "localEulerAngles") { p.type = FieldType::Vector3; p.euler = true; }
    else { err = "Transform has no animatable '" + field + "' (localPosition, localRotation, localEulerAngles, localScale)"; return false; }
    if (part < 0 || part > (p.type == FieldType::Quaternion ? 3 : 2)) { err = "'" + property + "' needs a channel: .x .y .z" + (p.type == FieldType::Quaternion ? " .w" : ""); return false; }
    p.fieldHash = fnv1a32(field.c_str());
    p.part = part;
    return true;
  }
  if (comp == "GameObject") {
    if (rest != "active") { err = "GameObject has only 'active' to animate"; return false; }
    p.fieldHash = fnv1a32("active");
    p.type = FieldType::Bool;
    return true;
  }
  const TypeInfo* ti = TypeRegistry::instance().find(comp.c_str());
  if (!ti) { err = "unknown component type '" + comp + "' in '" + property + "' (script components are known only when the cooker is built with the project's scripts)"; return false; }
  FieldPath fp;
  if (!parseFieldPath(*ti, rest.c_str(), fp) || !fp.field) { err = "'" + comp + "' has no field path '" + rest + "'"; return false; }
  const FieldDesc& fd = *fp.field;
  p.desc = &fd;
  p.fieldHash = fnv1a32(fd.name);
  p.type = fd.type;
  p.part = fp.part;
  switch (fd.type) {
    case FieldType::Scalar: case FieldType::I32: case FieldType::Bool: case FieldType::Enum: case FieldType::Guid:
      if (fp.part >= 0) { err = "'" + property + "': this field has no channels"; return false; }
      break;
    case FieldType::Vector2: case FieldType::Vector3: case FieldType::Quaternion: case FieldType::Color32:
      if (fp.part < 0) { err = "'" + property + "' needs a channel (x y z w or r g b a)"; return false; }
      break;
    default:
      err = "'" + property + "': " + fieldTypeName(fd.type) + " fields cannot be animated";
      return false;
  }
  return true;
}

bool keyValue(const json& v, const Prop& p, Scalar& out, std::string& err) {
  if (p.type == FieldType::Bool) {
    if (v.is_boolean()) { out = v.get<bool>() ? 1.0f : 0.0f; return true; }
  } else if (p.type == FieldType::Enum && v.is_string() && p.desc) {
    const i32 idx = enumIndexOf(*p.desc, v.get<std::string>().c_str());
    if (idx < 0) { err = "'" + v.get<std::string>() + "' is not a value of " + p.desc->name; return false; }
    out = static_cast<Scalar>(idx);
    return true;
  }
  if (!v.is_number()) { err = "key value must be a number"; return false; }
  out = v.get<float>();
  if (!std::isfinite(out)) { err = "key value must be finite"; return false; }
  if (p.type == FieldType::Color32 && (out < 0 || out > 255)) { err = "Color32 channels are 0..255"; return false; }
  return true;
}

struct RawKey { u32 tick; Scalar v, in, out; Guid g; };
struct RawCurve {
  std::string target;
  Prop prop;
  u8 interp = 0;
  std::vector<RawKey> keys;
};

// Unity's ZXY euler order (degrees) to a quaternion; host-only trig, the runtime never sees angles.
void eulerToQuat(Scalar ex, Scalar ey, Scalar ez, Scalar q[4]) {
  const double d2r = 3.14159265358979323846 / 180.0;
  const double hx = ex * d2r * 0.5, hy = ey * d2r * 0.5, hz = ez * d2r * 0.5;
  const double sx = sin(hx), cx = cos(hx), sy = sin(hy), cy = cos(hy), sz = sin(hz), cz = cos(hz);
  q[0] = static_cast<Scalar>(cy * sx * cz + sy * cx * sz);
  q[1] = static_cast<Scalar>(sy * cx * cz - cy * sx * sz);
  q[2] = static_cast<Scalar>(cy * cx * sz - sy * sx * cz);
  q[3] = static_cast<Scalar>(cy * cx * cz + sy * sx * sz);
}

Scalar evalLinear(const std::vector<RawKey>& k, Scalar t) {
  if (k.empty()) return 0;
  if (t <= k.front().tick) return k.front().v;
  if (t >= k.back().tick) return k.back().v;
  for (size_t i = 0; i + 1 < k.size(); ++i)
    if (t >= k[i].tick && t < k[i + 1].tick) {
      const Scalar s = (t - k[i].tick) / static_cast<Scalar>(k[i + 1].tick - k[i].tick);
      return k[i].v + (k[i + 1].v - k[i].v) * s;
    }
  return k.back().v;
}

}  // namespace

bool bakePropClip(const json& doc, const Guid& guid, const AddStringFn& addStr, std::vector<u8>& blob, PackPropClip& out, std::string& err) {
  out = PackPropClip();
  try {
    if (doc.value("format", 0) != 1) { err = "unsupported format version"; return false; }
    const std::string name = doc.value("name", std::string());
    const u32 fps = doc.value("fps", 30u);
    if (fps == 0 || fps > 240) { err = "\"fps\" must be 1..240"; return false; }
    std::vector<RawCurve> raw;
    u32 maxTick = 0;
    if (doc.contains("curves") && !doc["curves"].is_array()) { err = "\"curves\" must be an array"; return false; }
    for (const json& cj : doc.value("curves", json::array())) {
      RawCurve rc;
      rc.target = cj.value("target", std::string());
      if (!rc.target.empty() && (rc.target.front() == '/' || rc.target.back() == '/')) { err = "curve target '" + rc.target + "' must be a relative path like Arm/Hand"; return false; }
      const std::string property = cj.value("property", std::string());
      std::string e;
      if (!resolveProperty(property, rc.prop, e)) { err = e; return false; }
      const std::string interp = cj.value("interp", std::string("linear"));
      rc.interp = interp == "step" ? kPropInterpStep : (interp == "smooth" ? kPropInterpHermite : kPropInterpLinear);
      if (interp != "linear" && interp != "step" && interp != "smooth") { err = "'" + property + "': interp must be linear, step or smooth"; return false; }
      if (!cj.contains("keys") || !cj["keys"].is_array() || cj["keys"].empty()) { err = "'" + property + "' has no keys"; return false; }
      if (cj["keys"].size() > kMaxKeys) { err = "'" + property + "' has more than " + std::to_string(kMaxKeys) + " keys"; return false; }
      for (const json& kj : cj["keys"]) {
        RawKey k = {};
        const long long t = kj.value("t", -1LL);
        if (t < 0 || t > 100000) { err = "'" + property + "': key time \"t\" must be a tick count 0..100000"; return false; }
        k.tick = static_cast<u32>(t);
        if (!kj.contains("v")) { err = "'" + property + "': a key has no \"v\""; return false; }
        if (rc.prop.type == FieldType::Guid) {
          if (!kj["v"].is_string() || !Guid::parse(kj["v"].get<std::string>().c_str(), k.g)) { err = "'" + property + "': Guid keys need a guid string"; return false; }
        } else if (!keyValue(kj["v"], rc.prop, k.v, e)) { err = "'" + property + "': " + e; return false; }
        k.in = kj.value("in", 0.0f);
        k.out = kj.value("out", 0.0f);
        rc.keys.push_back(k);
        maxTick = std::max(maxTick, k.tick);
      }
      std::stable_sort(rc.keys.begin(), rc.keys.end(), [](const RawKey& a, const RawKey& b) { return a.tick < b.tick; });
      for (size_t i = 1; i < rc.keys.size(); ++i)
        if (rc.keys[i].tick == rc.keys[i - 1].tick) { err = "'" + property + "' has two keys at tick " + std::to_string(rc.keys[i].tick); return false; }
      raw.push_back(std::move(rc));
    }
    // localEulerAngles channels become localRotation x y z w keys (sampled per integer tick, hemisphere continuous).
    std::vector<RawCurve> curves;
    std::map<std::string, std::vector<const RawCurve*>> eulers;
    for (const RawCurve& rc : raw) {
      if (rc.prop.euler) eulers[rc.target].push_back(&rc);
      else curves.push_back(rc);
    }
    for (auto& kv : eulers) {
      const RawCurve* ch[3] = {nullptr, nullptr, nullptr};
      std::set<u32> ticks;
      u32 lo = 0xFFFFFFFFu, hi = 0;
      for (const RawCurve* rc : kv.second) {
        ch[rc->prop.part] = rc;
        for (const RawKey& k : rc->keys) { ticks.insert(k.tick); lo = std::min(lo, k.tick); hi = std::max(hi, k.tick); }
      }
      if (hi - lo <= 1200)
        for (u32 t = lo; t <= hi; ++t) ticks.insert(t);
      RawCurve q[4];
      for (int c = 0; c < 4; ++c) {
        q[c].target = kv.first;
        q[c].prop.componentType = kTransformTypeHash;
        q[c].prop.fieldHash = fnv1a32("localRotation");
        q[c].prop.part = c;
        q[c].prop.type = FieldType::Quaternion;
        q[c].interp = kPropInterpLinear;
      }
      Scalar prev[4] = {0, 0, 0, 1};
      for (u32 t : ticks) {
        Scalar e[3];
        for (int c = 0; c < 3; ++c) e[c] = ch[c] ? evalLinear(ch[c]->keys, static_cast<Scalar>(t)) : 0;
        Scalar quat[4];
        eulerToQuat(e[0], e[1], e[2], quat);
        if (quat[0] * prev[0] + quat[1] * prev[1] + quat[2] * prev[2] + quat[3] * prev[3] < 0)
          for (Scalar& v : quat) v = -v;
        for (int c = 0; c < 4; ++c) {
          prev[c] = quat[c];
          RawKey k = {};
          k.tick = t;
          k.v = quat[c];
          q[c].keys.push_back(k);
        }
      }
      for (int c = 0; c < 4; ++c) curves.push_back(q[c]);
    }
    if (curves.size() > kMaxCurves) { err = "more than " + std::to_string(kMaxCurves) + " curves"; return false; }
    std::vector<PackPropCurve> pcs;
    for (const RawCurve& rc : curves) {
      PackPropCurve pc = {};
      pc.pathStr = addStr(rc.target);
      pc.componentType = rc.prop.componentType;
      pc.fieldHash = rc.prop.fieldHash;
      pc.part = static_cast<i8>(rc.prop.part);
      pc.valueType = static_cast<u8>(rc.prop.type);
      const bool discrete = rc.prop.type == FieldType::I32 || rc.prop.type == FieldType::Bool || rc.prop.type == FieldType::Enum || rc.prop.type == FieldType::Guid;
      pc.interp = discrete ? kPropInterpStep : rc.interp;
      std::vector<PackPropKey> keys;
      for (const RawKey& k : rc.keys) keys.push_back({k.tick, k.v, k.in, k.out});
      pc.keyCount = static_cast<u32>(keys.size());
      pc.keysOffset = putBlob(blob, keys);
      if (rc.prop.type == FieldType::Guid) {
        std::vector<Guid> gs;
        for (const RawKey& k : rc.keys) gs.push_back(k.g);
        pc.reserved[0] = putBlob(blob, gs);
      }
      pcs.push_back(pc);
    }
    std::vector<PackPropEvent> evs;
    if (doc.contains("events") && !doc["events"].is_array()) { err = "\"events\" must be an array"; return false; }
    for (const json& ej : doc.value("events", json::array())) {
      PackPropEvent pe = {};
      const long long t = ej.value("t", -1LL);
      const std::string fn = ej.value("function", std::string());
      if (t < 0 || t > 100000) { err = "event time \"t\" must be a tick count 0..100000"; return false; }
      if (fn.empty() || fn.size() > 63) { err = "an event needs a \"function\" name (up to 63 characters)"; return false; }
      pe.tick = static_cast<u32>(t);
      pe.functionStr = addStr(fn);
      pe.intParameter = ej.value("int", 0);
      pe.floatParameter = ej.value("float", 0.0f);
      pe.stringStr = addStr(ej.value("string", std::string()));
      evs.push_back(pe);
      maxTick = std::max(maxTick, pe.tick);
    }
    if (evs.size() > kMaxEvents) { err = "more than " + std::to_string(kMaxEvents) + " events"; return false; }
    std::stable_sort(evs.begin(), evs.end(), [](const PackPropEvent& a, const PackPropEvent& b) { return a.tick < b.tick; });
    out.guid = guid;
    out.nameStr = addStr(name);
    out.fps = fps;
    out.lengthTicks = doc.contains("length") ? doc.value("length", 0u) : maxTick;
    if (out.lengthTicks < maxTick) { err = "\"length\" " + std::to_string(out.lengthTicks) + " is shorter than the last key or event (tick " + std::to_string(maxTick) + ")"; return false; }
    out.flags = doc.value("loop", false) ? kPropClipLoop : 0;
    out.curveCount = static_cast<u32>(pcs.size());
    out.curvesOffset = pcs.empty() ? 0 : putBlob(blob, pcs);
    out.eventCount = static_cast<u32>(evs.size());
    out.eventsOffset = evs.empty() ? 0 : putBlob(blob, evs);
  } catch (const std::exception& e) {
    err = e.what();
    return false;
  }
  return true;
}

bool bakeController(const json& doc, const Guid& guid, u32 fixedHz, const MotionResolver& motions, const AddStringFn& addStr,
                    std::vector<u8>& blob, PackAnimController& out, std::string& err) {
  out = PackAnimController();
  try {
    if (doc.value("format", 0) != 1) { err = "unsupported format version"; return false; }
    std::vector<PackAnimParam> params;
    std::map<std::string, u32> paramIdx;
    std::vector<PackAnimParamType> paramType;
    for (const json& pj : doc.value("parameters", json::array())) {
      const std::string n = pj.value("name", std::string());
      const std::string t = pj.value("type", std::string("float"));
      if (n.empty() || paramIdx.count(n)) { err = "parameter names must be unique and not empty ('" + n + "')"; return false; }
      PackAnimParam p = {};
      p.nameHash = fnv1a32(n.c_str());
      p.nameStr = addStr(n);
      if (t == "float") { p.type = static_cast<u32>(PackAnimParamType::Float); p.defaultBits = bits(pj.value("default", 0.0f)); }
      else if (t == "int") { p.type = static_cast<u32>(PackAnimParamType::Int); p.defaultBits = static_cast<u32>(pj.value("default", 0)); }
      else if (t == "bool") { p.type = static_cast<u32>(PackAnimParamType::Bool); p.defaultBits = pj.value("default", false) ? 1u : 0u; }
      else if (t == "trigger") { p.type = static_cast<u32>(PackAnimParamType::Trigger); p.defaultBits = 0; }
      else { err = "parameter '" + n + "': type must be float, int, bool or trigger"; return false; }
      for (const PackAnimParam& q : params)
        if (q.nameHash == p.nameHash) { err = "parameter '" + n + "' has the same name hash as another parameter"; return false; }
      paramIdx[n] = static_cast<u32>(params.size());
      paramType.push_back(static_cast<PackAnimParamType>(p.type));
      params.push_back(p);
    }
    if (params.size() > kMaxParams) { err = "more than " + std::to_string(kMaxParams) + " parameters"; return false; }
    const json states = doc.value("states", json::array());
    if (states.empty()) { err = "a controller needs at least one state"; return false; }
    if (states.size() > kMaxStates) { err = "more than " + std::to_string(kMaxStates) + " states"; return false; }
    std::map<std::string, u32> stateIdx;
    std::vector<PackAnimState> sts;
    for (const json& sj : states) {
      const std::string n = sj.value("name", std::string());
      if (n.empty() || stateIdx.count(n)) { err = "state names must be unique and not empty ('" + n + "')"; return false; }
      PackAnimState st = {};
      st.nameHash = fnv1a32(n.c_str());
      st.nameStr = addStr(n);
      const std::string motion = sj.value("motion", std::string());
      if (motion.empty()) {
        st.kind = static_cast<u32>(PackAnimStateKind::Empty);
      } else {
        PackAnimStateKind kind = PackAnimStateKind::Empty;
        std::string e;
        if (!motions.resolve || !motions.resolve(motion, kind, st.clip, e)) { err = "state '" + n + "': " + (e.empty() ? "motion " + motion + " is not a property clip or a skeletal clip" : e); return false; }
        st.kind = static_cast<u32>(kind);
      }
      st.flags = sj.value("loop", true) ? kAnimStateLoop : 0;
      st.speed = sj.value("speed", 1.0f);
      st.speedParam = kInvalidIndex;
      const std::string sp = sj.value("speedParameter", std::string());
      if (!sp.empty()) {
        auto it = paramIdx.find(sp);
        if (it == paramIdx.end() || paramType[it->second] != PackAnimParamType::Float) { err = "state '" + n + "': speedParameter '" + sp + "' is not a float parameter"; return false; }
        st.speedParam = it->second;
      }
      stateIdx[n] = static_cast<u32>(sts.size());
      sts.push_back(st);
    }
    const std::string def = doc.value("defaultState", std::string());
    u32 defaultState = 0;
    if (!def.empty()) {
      auto it = stateIdx.find(def);
      if (it == stateIdx.end()) { err = "defaultState '" + def + "' is not a state"; return false; }
      defaultState = it->second;
    }
    struct RawTrans { int from; PackAnimTransition t; std::vector<PackAnimCondition> conds; };
    std::vector<RawTrans> rts;
    for (const json& tj : doc.value("transitions", json::array())) {
      RawTrans r = {};
      const std::string from = tj.value("from", std::string());
      const std::string to = tj.value("to", std::string());
      if (from == "*" || from == "Any" || from == "AnyState") r.from = -1;
      else if (stateIdx.count(from)) r.from = static_cast<int>(stateIdx[from]);
      else { err = "transition from '" + from + "': no such state"; return false; }
      if (!stateIdx.count(to)) { err = "transition to '" + to + "': no such state"; return false; }
      r.t.toState = static_cast<i32>(stateIdx[to]);
      r.t.fromState = r.from;
      if (tj.value("hasExitTime", false)) r.t.flags |= kAnimTransHasExitTime;
      r.t.exitTime = tj.value("exitTime", 1.0f);
      const Scalar dur = tj.value("duration", 0.0f);
      if (dur < 0 || dur > 60) { err = "transition " + from + " -> " + to + ": duration must be 0..60 seconds"; return false; }
      r.t.durationTicks = static_cast<u32>(dur * static_cast<Scalar>(fixedHz) + 0.5f);
      for (const json& cj : tj.value("conditions", json::array())) {
        const std::string pn = cj.value("param", std::string());
        auto it = paramIdx.find(pn);
        if (it == paramIdx.end()) { err = "transition " + from + " -> " + to + ": condition names the missing parameter '" + pn + "'"; return false; }
        const std::string mode = cj.value("mode", std::string("if"));
        PackAnimCondition c = {};
        c.param = it->second;
        const PackAnimParamType pt = paramType[it->second];
        if (mode == "if") c.mode = static_cast<u32>(PackAnimCondMode::If);
        else if (mode == "ifNot") c.mode = static_cast<u32>(PackAnimCondMode::IfNot);
        else if (mode == "greater") c.mode = static_cast<u32>(PackAnimCondMode::Greater);
        else if (mode == "less") c.mode = static_cast<u32>(PackAnimCondMode::Less);
        else if (mode == "equals") c.mode = static_cast<u32>(PackAnimCondMode::Equals);
        else if (mode == "notEqual") c.mode = static_cast<u32>(PackAnimCondMode::NotEqual);
        else { err = "condition mode '" + mode + "' is not one of if ifNot greater less equals notEqual"; return false; }
        const bool cmp = c.mode >= static_cast<u32>(PackAnimCondMode::Greater);
        if (cmp && (pt == PackAnimParamType::Bool || pt == PackAnimParamType::Trigger)) { err = "parameter '" + pn + "' is a bool or trigger: use if / ifNot"; return false; }
        if (!cmp && (pt == PackAnimParamType::Float || pt == PackAnimParamType::Int)) { err = "parameter '" + pn + "' is a number: use greater / less / equals / notEqual"; return false; }
        if (cmp) c.thresholdBits = pt == PackAnimParamType::Float ? bits(cj.value("value", 0.0f)) : static_cast<u32>(cj.value("value", 0));
        r.conds.push_back(c);
      }
      rts.push_back(std::move(r));
    }
    std::vector<PackAnimTransition> ts;
    std::vector<PackAnimCondition> cs;
    auto emit = [&](RawTrans& r) {
      r.t.firstCondition = static_cast<u32>(cs.size());
      r.t.conditionCount = static_cast<u32>(r.conds.size());
      cs.insert(cs.end(), r.conds.begin(), r.conds.end());
      ts.push_back(r.t);
    };
    for (size_t si = 0; si < sts.size(); ++si) {
      sts[si].firstTransition = static_cast<u32>(ts.size());
      for (RawTrans& r : rts) if (r.from == static_cast<int>(si)) emit(r);
      sts[si].transitionCount = static_cast<u32>(ts.size()) - sts[si].firstTransition;
    }
    const u32 anyFirst = static_cast<u32>(ts.size());
    for (RawTrans& r : rts) if (r.from < 0) emit(r);
    out.guid = guid;
    out.nameStr = addStr(doc.value("name", std::string()));
    out.defaultState = defaultState;
    out.paramCount = static_cast<u32>(params.size());
    out.paramsOffset = params.empty() ? 0 : putBlob(blob, params);
    out.stateCount = static_cast<u32>(sts.size());
    out.statesOffset = putBlob(blob, sts);
    out.transitionCount = static_cast<u32>(ts.size());
    out.transitionsOffset = ts.empty() ? 0 : putBlob(blob, ts);
    out.conditionCount = static_cast<u32>(cs.size());
    out.conditionsOffset = cs.empty() ? 0 : putBlob(blob, cs);
    out.anyFirst = anyFirst;
    out.anyCount = static_cast<u32>(ts.size()) - anyFirst;
  } catch (const std::exception& e) {
    err = e.what();
    return false;
  }
  return true;
}

namespace {
bool readFile(const std::filesystem::path& p, std::string& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}
}  // namespace

bool cookAnimationAssets(const std::vector<AnimAssetRef>& clipAssets, const std::vector<AnimAssetRef>& controllerAssets, u32 fixedHz,
                         const std::function<bool(const std::string&, Guid&)>& skeletal, const AddStringFn& addStr, std::vector<u8>& blob,
                         AnimCooked& out, std::vector<AnimIssue>& issues) {
  const size_t before = issues.size();
  std::map<std::string, Guid> clipGuid;
  for (const AnimAssetRef& a : clipAssets) {
    Guid g;
    if (!Guid::parse(a.guid.c_str(), g)) { issues.push_back({"bad GUID '" + a.guid + "'", a.rel, a.guid}); continue; }
    std::string txt, err;
    PackPropClip pc;
    if (!readFile(a.path, txt)) { issues.push_back({"cannot read the clip", a.rel, a.guid}); continue; }
    json doc = json::parse(txt, nullptr, false);
    if (doc.is_discarded()) { issues.push_back({"invalid JSON", a.rel, a.guid}); continue; }
    if (!bakePropClip(doc, g, addStr, blob, pc, err)) { issues.push_back({err, a.rel, a.guid}); continue; }
    clipGuid[a.guid] = g;
    out.clips.push_back(pc);
  }
  MotionResolver mr;
  mr.resolve = [&](const std::string& gt, PackAnimStateKind& kind, Guid& g, std::string& err) {
    auto it = clipGuid.find(gt);
    if (it != clipGuid.end()) { kind = PackAnimStateKind::PropClip; g = it->second; return true; }
    if (skeletal && skeletal(gt, g)) { kind = PackAnimStateKind::Clip3; return true; }
    err = "motion " + gt + " is neither a property clip of the project nor a skeletal clip (glTF with animation)";
    return false;
  };
  for (const AnimAssetRef& a : controllerAssets) {
    Guid g;
    if (!Guid::parse(a.guid.c_str(), g)) { issues.push_back({"bad GUID '" + a.guid + "'", a.rel, a.guid}); continue; }
    std::string txt, err;
    PackAnimController pc;
    if (!readFile(a.path, txt)) { issues.push_back({"cannot read the controller", a.rel, a.guid}); continue; }
    json doc = json::parse(txt, nullptr, false);
    if (doc.is_discarded()) { issues.push_back({"invalid JSON", a.rel, a.guid}); continue; }
    if (!bakeController(doc, g, fixedHz, mr, addStr, blob, pc, err)) { issues.push_back({err, a.rel, a.guid}); continue; }
    out.controllers.push_back(pc);
  }
  return issues.size() == before;
}

}  // namespace cooker
}  // namespace rensga
