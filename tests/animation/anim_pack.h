// F6.03 (B1) test helper: a tiny in-memory pack with objects, components, property clips (PCLP) and animator controllers (ANIC),
// so the Animator runtime and the property clip sampler are tested without the cooker. Layout as in docs/rensga-engine/pack-format.md.
#pragma once
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "rensga/core/pack_format.h"
#include "rensga/core/prop_clip.h"
#include "rensga/core/reflect.h"

namespace animpack {
using namespace rensga;

struct KeyDef { u32 tick; Scalar value; Scalar inTan = 0, outTan = 0; };
struct CurveDef {
  std::string path, component, field;
  int part = -1;
  u8 interp = 0;
  FieldType type = FieldType::Scalar;
  std::vector<KeyDef> keys;
  std::vector<Guid> guids;  // Guid curves: one per key
};
struct EventDef {
  u32 tick;
  std::string fn;
  i32 i;
  Scalar f;
  std::string s;
  EventDef(u32 t, const char* fn_, i32 i_ = 0, Scalar f_ = 0, const char* s_ = "") : tick(t), fn(fn_), i(i_), f(f_), s(s_) {}
};
struct ParamDef { std::string name; PackAnimParamType type; u32 defBits = 0; };
struct CondDef { u32 param; PackAnimCondMode mode; u32 bits = 0; };
struct TransDef { int from; int to; bool exit = false; Scalar exitTime = 1; u32 ticks = 0; std::vector<CondDef> conds; };
struct StateDef { std::string name; PackAnimStateKind kind; Guid clip; bool loop = true; Scalar speed = 1; u32 speedParam = kInvalidIndex; };

inline u32 bitsOf(Scalar f) { u32 b; memcpy(&b, &f, 4); return b; }
inline Guid guidN(u8 n) { Guid g; g.bytes[0] = 0xC1; g.bytes[15] = n; return g; }

struct Builder {
  struct Sec { u32 fourcc; std::vector<u8> bytes; u32 count; };
  std::vector<PackObject> objs;
  std::vector<PackTransform> xf;
  std::vector<PackComponent> comps;
  std::vector<u8> fdat;
  std::vector<u8> strs{0};
  std::vector<u8> blob;
  std::vector<PackPropClip> clips;
  std::vector<PackAnimController> ctls;

  u32 str(const char* s) {
    u32 off = static_cast<u32>(strs.size());
    strs.insert(strs.end(), s, s + strlen(s) + 1);
    return off;
  }
  u32 blobPut(const void* p, size_t n) {
    while (blob.size() % 16) blob.push_back(0);
    u32 off = static_cast<u32>(blob.size());
    blob.insert(blob.end(), static_cast<const u8*>(p), static_cast<const u8*>(p) + n);
    return off;
  }
  template <class T>
  u32 blobPutV(const std::vector<T>& v) { return blobPut(v.empty() ? static_cast<const void*>(&v) : static_cast<const void*>(v.data()), v.size() * sizeof(T)); }

  u32 addObject(const char* name, int parent = -1, bool active = true) {
    PackObject o = {};
    o.nameStr = str(name);
    o.parentIndex = parent;
    o.active = active ? 1 : 0;
    objs.push_back(o);
    PackTransform t = {};
    t.rotation[3] = 1;
    t.scale[0] = t.scale[1] = t.scale[2] = 1;
    xf.push_back(t);
    return static_cast<u32>(objs.size() - 1);
  }
  // Adds a component of `ti`, default constructed, then `init(obj)` edits it before it is written.
  void addComponent(u32 obj, const TypeInfo& ti, const std::function<void(void*)>& init = nullptr) {
    std::vector<u8> mem(ti.size + 64);
    ti.construct(mem.data());
    if (init) init(mem.data());
    std::vector<u8> b(blobSize(ti));
    writeBlob(ti, mem.data(), b.data());
    PackComponent pc = {};
    pc.typeHash = ti.hash;
    pc.objectIndex = obj;
    pc.fieldOffset = static_cast<u32>(fdat.size());
    pc.fieldSize = static_cast<u32>(b.size());
    fdat.insert(fdat.end(), b.begin(), b.end());
    while (fdat.size() % 16) fdat.push_back(0);
    comps.push_back(pc);
    if (ti.destruct) ti.destruct(mem.data());
  }

  Guid addClip(u8 id, const char* name, u32 fps, u32 lengthTicks, bool loop, const std::vector<CurveDef>& curves, const std::vector<EventDef>& events = {}) {
    std::vector<PackPropCurve> pcs;
    for (const CurveDef& c : curves) {
      std::vector<PackPropKey> keys;
      for (const KeyDef& k : c.keys) keys.push_back({k.tick, k.value, k.inTan, k.outTan});
      PackPropCurve pc = {};
      pc.pathStr = str(c.path.c_str());
      pc.componentType = fnv1a32(c.component.c_str());
      pc.fieldHash = fnv1a32(c.field.c_str());
      pc.part = static_cast<i8>(c.part);
      pc.interp = c.interp;
      pc.valueType = static_cast<u8>(c.type);
      pc.keyCount = static_cast<u32>(keys.size());
      pc.keysOffset = blobPutV(keys);
      if (!c.guids.empty()) pc.reserved[0] = blobPutV(c.guids);
      pcs.push_back(pc);
    }
    std::vector<PackPropEvent> evs;
    for (const EventDef& e : events) {
      PackPropEvent pe = {};
      pe.tick = e.tick;
      pe.functionStr = str(e.fn.c_str());
      pe.intParameter = e.i;
      pe.floatParameter = e.f;
      pe.stringStr = str(e.s.c_str());
      evs.push_back(pe);
    }
    PackPropClip pc = {};
    pc.guid = guidN(id);
    pc.nameStr = str(name);
    pc.fps = fps;
    pc.lengthTicks = lengthTicks;
    pc.flags = loop ? kPropClipLoop : 0;
    pc.curveCount = static_cast<u32>(pcs.size());
    pc.curvesOffset = pcs.empty() ? 0 : blobPutV(pcs);
    pc.eventCount = static_cast<u32>(evs.size());
    pc.eventsOffset = evs.empty() ? 0 : blobPutV(evs);
    clips.push_back(pc);
    return pc.guid;
  }

  Guid addController(u8 id, const char* name, const std::vector<ParamDef>& params, const std::vector<StateDef>& states, const std::vector<TransDef>& trans, u32 defaultState = 0) {
    std::vector<PackAnimParam> ps;
    for (const ParamDef& p : params) ps.push_back({fnv1a32(p.name.c_str()), str(p.name.c_str()), static_cast<u32>(p.type), p.defBits});
    // transitions grouped by source state, then any-state ones
    std::vector<PackAnimTransition> ts;
    std::vector<PackAnimCondition> cs;
    std::vector<PackAnimState> sts;
    auto emit = [&](const TransDef& t) {
      PackAnimTransition pt = {};
      pt.toState = t.to;
      pt.flags = t.exit ? kAnimTransHasExitTime : 0;
      pt.exitTime = t.exitTime;
      pt.durationTicks = t.ticks;
      pt.firstCondition = static_cast<u32>(cs.size());
      pt.conditionCount = static_cast<u32>(t.conds.size());
      pt.fromState = t.from;
      for (const CondDef& c : t.conds) cs.push_back({c.param, static_cast<u32>(c.mode), c.bits, 0});
      ts.push_back(pt);
    };
    for (size_t si = 0; si < states.size(); ++si) {
      PackAnimState st = {};
      st.nameHash = fnv1a32(states[si].name.c_str());
      st.nameStr = str(states[si].name.c_str());
      st.kind = static_cast<u32>(states[si].kind);
      st.flags = states[si].loop ? kAnimStateLoop : 0;
      st.clip = states[si].clip;
      st.speed = states[si].speed;
      st.speedParam = states[si].speedParam;
      st.firstTransition = static_cast<u32>(ts.size());
      for (const TransDef& t : trans) if (t.from == static_cast<int>(si)) emit(t);
      st.transitionCount = static_cast<u32>(ts.size()) - st.firstTransition;
      sts.push_back(st);
    }
    const u32 anyFirst = static_cast<u32>(ts.size());
    for (const TransDef& t : trans) if (t.from < 0) emit(t);
    PackAnimController c = {};
    c.guid = guidN(id);
    c.nameStr = str(name);
    c.defaultState = defaultState;
    c.paramCount = static_cast<u32>(ps.size());
    c.paramsOffset = ps.empty() ? 0 : blobPutV(ps);
    c.stateCount = static_cast<u32>(sts.size());
    c.statesOffset = sts.empty() ? 0 : blobPutV(sts);
    c.transitionCount = static_cast<u32>(ts.size());
    c.transitionsOffset = ts.empty() ? 0 : blobPutV(ts);
    c.conditionCount = static_cast<u32>(cs.size());
    c.conditionsOffset = cs.empty() ? 0 : blobPutV(cs);
    c.anyFirst = anyFirst;
    c.anyCount = static_cast<u32>(ts.size()) - anyFirst;
    ctls.push_back(c);
    return c.guid;
  }

  template <class T>
  static Sec sec(u32 fourcc, const std::vector<T>& v) {
    Sec s;
    s.fourcc = fourcc;
    const u8* p = reinterpret_cast<const u8*>(v.data());
    s.bytes.assign(p, p + v.size() * sizeof(T));
    s.count = static_cast<u32>(v.size());
    return s;
  }
  // 16-byte aligned memory the caller free()s.
  u8* build(size_t* size) {
    std::vector<Sec> all;
    all.push_back(sec(pack_section::OBJS, objs));
    all.push_back(sec(pack_section::XFRM, xf));
    all.push_back(sec(pack_section::COMP, comps));
    Sec f;
    f.fourcc = pack_section::FDAT;
    f.bytes = fdat.empty() ? std::vector<u8>(16, 0) : fdat;
    f.count = static_cast<u32>(comps.size());
    all.push_back(f);
    if (!clips.empty()) all.push_back(sec(pack_section::PCLP, clips));
    if (!ctls.empty()) all.push_back(sec(pack_section::ANIC, ctls));
    Sec s;
    s.fourcc = pack_section::STRS;
    s.bytes = strs;
    s.count = 1;
    all.push_back(s);
    Sec b;
    b.fourcc = pack_section::BLOB;
    b.bytes = blob.empty() ? std::vector<u8>(16, 0) : blob;
    b.count = 0;
    all.push_back(b);
    size_t off = (sizeof(PackHeader) + all.size() * sizeof(PackSection) + 15) & ~size_t(15);
    std::vector<PackSection> table(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
      table[i] = {all[i].fourcc, static_cast<u32>(off), static_cast<u32>(all[i].bytes.size()), all[i].count};
      off = (off + all[i].bytes.size() + 15) & ~size_t(15);
    }
    u8* mem = static_cast<u8*>(aligned_alloc(16, (off + 15) & ~size_t(15)));
    memset(mem, 0, off);
    PackHeader h = {};
    memcpy(h.magic, "RPAK", 4);
    h.version = kPackVersion;
    h.sectionCount = static_cast<u32>(all.size());
    h.alignment = 16;
    h.totalSize = static_cast<u32>(off);
    memcpy(mem, &h, sizeof h);
    memcpy(mem + sizeof h, table.data(), table.size() * sizeof(PackSection));
    for (size_t i = 0; i < all.size(); ++i) memcpy(mem + table[i].offset, all[i].bytes.data(), all[i].bytes.size());
    *size = off;
    return mem;
  }
};
}  // namespace animpack
