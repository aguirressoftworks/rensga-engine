// F6.03 / F6.10 (B1): Animator runtime and animation events. Everything is integer time plus float + - * / (no libm), so Linux, the
// PSP simulator and the console produce the same bits. Fixed capacities only: the slot table and the palette pool are statics.
#include "rensga/core/animator_system.h"

#include <string.h>

#include "rensga/core/animation.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/log.h"
#include "rensga/core/prop_clip.h"
#include "rensga/core/reflect_method.h"
#include "rensga/core/time.h"

namespace rensga {

// ---- animation events --------------------------------------------------------------------------------------------------------

namespace AnimationEvents {
namespace {
Handler g_handler = nullptr;

void defaultDispatch(GameObject* target, const AnimationEvent& event) {
  if (!target || !target->world || target->dying) return;
  World* world = target->world;
  const u32 nameHash = event.functionName && *event.functionName ? fnv1a32(event.functionName) : 0;
  for (u32 i = 0; i < target->componentCount; ++i) {
    const TypeInfo* ti = TypeRegistry::instance().byId(target->components[i].typeId);
    const ComponentPool* p = ti ? world->pool(*ti) : nullptr;
    if (!p || !ti->asBehaviour) continue;
    const u32 slot = target->components[i].slot;
    if ((p->flags[slot] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
    void* obj = p->at(slot);
    ti->asBehaviour(obj)->OnAnimationEvent(event);
    if (!nameHash) continue;
    const MethodInfo* m = MethodRegistry::instance().find(*ti, nameHash);
    if (!m || !m->invoke) continue;
    MethodArg arg;
    arg.s = event.floatParameter;
    arg.i = event.intParameter;
    arg.b = event.intParameter != 0;
    m->invoke(obj, arg);
  }
}
}  // namespace
void setHandler(Handler handler) { g_handler = handler; }
void dispatch(GameObject* target, const AnimationEvent& event) {
  if (g_handler) g_handler(target, event);
  else defaultDispatch(target, event);
}
}  // namespace AnimationEvents

bool AnimatorStateInfo::IsName(const char* name) const { return name && nameHash == fnv1a32(name); }
u32 Animator::StringToHash(const char* name) { return name ? fnv1a32(name) : 0; }

// ---- the system ----------------------------------------------------------------------------------------------------------------

namespace AnimatorSystem {
namespace {

struct Slot {
  bool used = false, seen = false;
  const Animator* owner = nullptr;
  Guid controller;
  const u8* packBase = nullptr;
  const PackAnimController* ctl = nullptr;
  u32 params[kMaxParams] = {};
  u32 cur = 0, next = kInvalidIndex;
  u64 curQ = 0, nextQ = 0;       // state time in exact integer units (see Motion): no drift, an event at tick t fires on the exact fixed step
  i64 curEv = -1, nextEv = -1;   // last event time handled (Q16 clip ticks), -1 = none yet
  u32 curLoops = 0, nextLoops = 0;
  bool curFresh = true, nextFresh = false;  // just entered: show the pose at time 0 before advancing
  u32 transTotal = 0, transElapsed = 0;
  u32 palOffset = 0, palCount = 0;
  bool hasPal = false;
};

Slot g_slots[kMaxAnimators];
Matrix4 g_pal[kPalettePool];
u32 g_palUsed = 0;
bool g_trace = false, g_warnedSlots = false, g_warnedPal = false, g_anyUsed = false;
u32 g_steps = 0, g_hash = 0;
constexpr u32 kMaxHooks = 4;
StepHook g_hooks[kMaxHooks];
u32 g_hookCount = 0;

// The pack the controllers come from (re-read when GfxAssets points at another one).
const PackView* g_view = nullptr;
PackContent g_content;
const char* g_strs = nullptr;
u32 g_strsSize = 0;

struct Motion {
  PackAnimStateKind kind = PackAnimStateKind::Empty;
  PropClipSource pc;
  const PackClip3* c3 = nullptr;
  // Time unit: property clip 1 / (65536 * tickHz) clip ticks, so one fixed step at speed s advances fps * (s * 65536) units and the
  // time of a step is an exact integer (a 12 fps clip at 60 Hz reaches tick 2 on step 10, not 11). Skeletal clips: 1/65536 fixed step.
  u64 lenQ = 0;        // clip length in units
  u64 mulFps = 1;      // units per fixed step at speed 1 are mulFps * 65536
  u32 hz = 60;
  u64 toQ16(u64 units) const { return kind == PackAnimStateKind::PropClip ? units / hz : units; }  // Q16 clip ticks (property clips)
};

u32 fold(u32 h, u32 v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (i * 8)) & 255u;
    h *= 16777619u;
  }
  return h;
}
Scalar bitsToFloat(u32 b) {
  Scalar f;
  memcpy(&f, &b, 4);
  return f;
}
u32 floatToBits(Scalar f) {
  u32 b;
  memcpy(&b, &f, 4);
  return b;
}

u32 tickHz() {
  const Scalar fd = Time::fixedDeltaTime();
  return fd > 0 ? static_cast<u32>(1.0f / fd + 0.5f) : 60u;
}

bool bindPack() {
  const PackView* v = GfxAssets::pack();
  if (!v || !v->header) return false;
  if (v != g_view || v->base != (g_view ? g_view->base : nullptr) || !g_content.blob) {
    g_view = v;
    g_content = v->content();
    const PackSection* s = v->find(pack_section::STRS);
    g_strs = s ? reinterpret_cast<const char*>(v->base + s->offset) : nullptr;
    g_strsSize = s ? s->size : 0;
  }
  return g_content.controllerCount > 0;
}

const PackAnimState* statesOf(const Slot& s) { return g_content.states(*s.ctl); }

bool motionOf(const Slot& s, u32 stateIdx, u32 hz, Motion& m) {
  m = Motion();
  if (stateIdx >= s.ctl->stateCount) return false;
  const PackAnimState& st = statesOf(s)[stateIdx];
  if (st.kind == static_cast<u32>(PackAnimStateKind::PropClip)) {
    const PackPropClip* c = g_content.findPropClip(st.clip);
    if (!c) return false;
    m.kind = PackAnimStateKind::PropClip;
    m.pc = PropClipSource{c, g_content.blob, g_content.blobSize, g_strs, g_strsSize};
    m.hz = hz;
    m.mulFps = c->fps;
    m.lenQ = static_cast<u64>(c->lengthTicks) * hz * 65536u;
    return true;
  }
  if (st.kind == static_cast<u32>(PackAnimStateKind::Clip3) && GfxAssets::bound()) {
    const PackAssets& as = GfxAssets::assets();
    const PackClip3* c = as.findClip3(st.clip);
    if (!c || c->fps == 0) return false;
    m.kind = PackAnimStateKind::Clip3;
    m.c3 = c;
    m.hz = hz;
    m.mulFps = 1;
    m.lenQ = ((static_cast<u64>(c->frameCount) * hz) << 16) / c->fps;
    return true;
  }
  return false;
}

bool stateLoops(const Slot& s, u32 idx) { return (statesOf(s)[idx].flags & kAnimStateLoop) != 0; }

i32 findState(const Slot& s, u32 nameHash) {
  const PackAnimState* st = statesOf(s);
  for (u32 i = 0; i < s.ctl->stateCount; ++i)
    if (st[i].nameHash == nameHash) return static_cast<i32>(i);
  return -1;
}

i32 findParam(const Slot& s, u32 hash) {
  const PackAnimParam* p = g_content.params(*s.ctl);
  const u32 n = s.ctl->paramCount < kMaxParams ? s.ctl->paramCount : kMaxParams;
  for (u32 i = 0; i < n; ++i)
    if (p[i].nameHash == hash) return static_cast<i32>(i);
  return -1;
}

void resetSlot(Slot& s, const Animator* owner, const PackAnimController* ctl) {
  s.used = true;
  s.owner = owner;
  s.ctl = ctl;
  s.controller = ctl->guid;
  s.packBase = g_view ? g_view->base : nullptr;
  const PackAnimParam* p = g_content.params(*ctl);
  const u32 n = ctl->paramCount < kMaxParams ? ctl->paramCount : kMaxParams;
  for (u32 i = 0; i < kMaxParams; ++i) s.params[i] = i < n ? p[i].defaultBits : 0;
  s.cur = ctl->defaultState < ctl->stateCount ? ctl->defaultState : 0;
  s.next = kInvalidIndex;
  s.curQ = s.nextQ = 0;
  s.curEv = s.nextEv = -1;
  s.curLoops = s.nextLoops = 0;
  s.curFresh = true;
  s.nextFresh = false;
  s.transTotal = s.transElapsed = 0;
}

// The slot of an Animator, created on demand. Null without a bound pack, a controller or a free slot.
Slot* slotOf(Animator& a, bool create) {
  if (a.runtimeIndex_ < kMaxAnimators && g_slots[a.runtimeIndex_].used && g_slots[a.runtimeIndex_].owner == &a) {
    Slot& s = g_slots[a.runtimeIndex_];
    if (s.controller == a.controller && g_view && s.packBase == g_view->base) return &s;
    if (!bindPack()) return &s;
    if (s.packBase != (g_view ? g_view->base : nullptr) || s.controller != a.controller) {
      const PackAnimController* ctl = g_content.findController(a.controller);
      if (!ctl) return nullptr;
      resetSlot(s, &a, ctl);
    }
    return &s;
  }
  if (!create || a.controller.isNull() || !bindPack()) return nullptr;
  const PackAnimController* ctl = g_content.findController(a.controller);
  if (!ctl || ctl->stateCount == 0) return nullptr;
  for (u32 i = 0; i < kMaxAnimators; ++i) {
    if (g_slots[i].used) continue;
    g_slots[i] = Slot();
    resetSlot(g_slots[i], &a, ctl);
    g_anyUsed = true;
    a.runtimeIndex_ = i;
    return &g_slots[i];
  }
  if (!g_warnedSlots) {
    g_warnedSlots = true;
    RLOG_WARN("anim", "more than %u Animators with a controller: the extra ones do nothing", kMaxAnimators);
  }
  return nullptr;
}

Slot* slotOfConst(const Animator& a) { return slotOf(const_cast<Animator&>(a), false); }

bool condTrue(const Slot& s, const PackAnimCondition& c) {
  const PackAnimParam& p = g_content.params(*s.ctl)[c.param];
  const u32 bits = c.param < kMaxParams ? s.params[c.param] : 0;
  switch (static_cast<PackAnimCondMode>(c.mode)) {
    case PackAnimCondMode::If: return bits != 0;
    case PackAnimCondMode::IfNot: return bits == 0;
    default: break;
  }
  if (p.type == static_cast<u32>(PackAnimParamType::Float)) {
    const Scalar v = bitsToFloat(bits), t = bitsToFloat(c.thresholdBits);
    switch (static_cast<PackAnimCondMode>(c.mode)) {
      case PackAnimCondMode::Greater: return v > t;
      case PackAnimCondMode::Less: return v < t;
      case PackAnimCondMode::Equals: return v == t;
      default: return v != t;
    }
  }
  const i32 v = static_cast<i32>(bits), t = static_cast<i32>(c.thresholdBits);
  switch (static_cast<PackAnimCondMode>(c.mode)) {
    case PackAnimCondMode::Greater: return v > t;
    case PackAnimCondMode::Less: return v < t;
    case PackAnimCondMode::Equals: return v == t;
    default: return v != t;
  }
}

u32 speedQ16(const Slot& s, u32 stateIdx, Scalar animatorSpeed) {
  const PackAnimState& st = statesOf(s)[stateIdx];
  Scalar sp = st.speed * animatorSpeed;
  if (st.speedParam != kInvalidIndex && st.speedParam < kMaxParams) sp *= bitsToFloat(s.params[st.speedParam]);
  if (sp < 0) sp = 0;
  if (sp > 60000.0f) return 0xFFFF0000u;
  return static_cast<u32>(sp * 65536.0f);
}

Scalar normalized(const Motion& m, u64 q, u32 loops) {
  if (m.lenQ == 0) return 1.0f;
  u64 len = m.lenQ;
  while (len > 0xFFFFFFu) { len >>= 1; q >>= 1; }  // keep both inside a float's exact integer range
  return static_cast<Scalar>(loops) + static_cast<Scalar>(static_cast<u32>(q)) / static_cast<Scalar>(static_cast<u32>(len));
}

// Moves a state forward by `adv` units, firing the property clip's events on the way.
void advanceState(const Motion& m, bool loop, u64 adv, u64& q, i64& ev, u32& loops, GameObject* go, bool fire) {
  const bool prop = m.kind == PackAnimStateKind::PropClip;
  if (m.lenQ == 0) {
    if (prop && fire) { PropClips::fireEvents(go, m.pc, ev, 0); ev = 0; }
    return;
  }
  u64 nq = q + adv;
  if (nq >= m.lenQ) {
    if (loop) {
      while (nq >= m.lenQ) {
        if (prop && fire) PropClips::fireEvents(go, m.pc, ev, static_cast<i64>(m.toQ16(m.lenQ)));
        nq -= m.lenQ;
        ev = -1;
        ++loops;
        if (adv == 0) break;
      }
    } else {
      nq = m.lenQ;
    }
  }
  if (prop && fire) PropClips::fireEvents(go, m.pc, ev, static_cast<i64>(m.toQ16(nq)));
  ev = static_cast<i64>(m.toQ16(nq));
  q = nq;
}

void blendPose(const PackJointPose* a, const PackJointPose* b, u32 n, Scalar w, PackJointPose* out) {
  const Scalar u = 1.0f - w;
  for (u32 j = 0; j < n; ++j) {
    Scalar d = a[j].r[0] * b[j].r[0] + a[j].r[1] * b[j].r[1] + a[j].r[2] * b[j].r[2] + a[j].r[3] * b[j].r[3];
    const Scalar sgn = d < 0 ? -1.0f : 1.0f;
    Quaternion q(a[j].r[0] * u + sgn * b[j].r[0] * w, a[j].r[1] * u + sgn * b[j].r[1] * w, a[j].r[2] * u + sgn * b[j].r[2] * w,
                 a[j].r[3] * u + sgn * b[j].r[3] * w);
    q = q.normalized();
    out[j].r[0] = q.x; out[j].r[1] = q.y; out[j].r[2] = q.z; out[j].r[3] = q.w;
    for (int k = 0; k < 3; ++k) out[j].t[k] = a[j].t[k] * u + b[j].t[k] * w;
    out[j].s = a[j].s * u + b[j].s * w;
  }
}

bool samplePose(const Slot& s, u32 stateIdx, const Motion& m, u64 q, u32 hz, PackJointPose* pose, u32* skinOut) {
  if (m.kind != PackAnimStateKind::Clip3 || !m.c3) return false;
  const PackAssets& as = GfxAssets::assets();
  if (m.c3->skin >= as.skinCount) return false;
  const PackSkin& skin = as.skins[m.c3->skin];
  if (skin.jointCount == 0 || skin.jointCount > 64) return false;
  PackClip3 eff = *m.c3;
  eff.loop = stateLoops(s, stateIdx) ? 1 : 0;
  sampleClip(eff, as.clipPoses(*m.c3), skin.jointCount, static_cast<u32>(q >> 16), hz, pose);
  *skinOut = m.c3->skin;
  return true;
}

void applySkeleton(Slot& s, const Motion* mc, const Motion* mn, u32 hz, Scalar w) {
  PackJointPose pa[64], pb[64], out[64];
  u32 skinA = 0, skinB = 0;
  const bool ha = mc && s.cur != kInvalidIndex && samplePose(s, s.cur, *mc, s.curQ, hz, pa, &skinA);
  const bool hb = mn && s.next != kInvalidIndex && samplePose(s, s.next, *mn, s.nextQ, hz, pb, &skinB);
  if (!ha && !hb) return;
  const PackAssets& as = GfxAssets::assets();
  const u32 skinIdx = ha ? skinA : skinB;
  const PackSkin& skin = as.skins[skinIdx];
  const PackJointPose* use = pa;
  if (ha && hb && skinA == skinB) { blendPose(pa, pb, skin.jointCount, w, out); use = out; }
  else if (!ha) use = pb;
  else if (hb && skinA != skinB && w >= 0.5f) use = pb;
  if (!s.hasPal || s.palCount != skin.jointCount) {
    if (g_palUsed + skin.jointCount > kPalettePool) {
      if (!g_warnedPal) { g_warnedPal = true; RLOG_WARN("anim", "animator palette pool full (%u matrices)", kPalettePool); }
      return;
    }
    s.palOffset = g_palUsed;
    s.palCount = skin.jointCount;
    s.hasPal = true;
    g_palUsed += skin.jointCount;
  }
  buildPalette(skin, as.skinJoints(skin), use, g_pal + s.palOffset);
}

void applyProps(World& world, GameObject* go, const Motion& m, u64 q, Scalar w) {
  if (m.kind != PackAnimStateKind::PropClip) return;
  PropClips::apply(world, go, m.pc, m.toQ16(q > m.lenQ ? m.lenQ : q), w, &g_hash);
}

void transitionStart(Slot& s, const PackAnimTransition& t) {
  if (t.durationTicks == 0) {
    s.cur = static_cast<u32>(t.toState);
    s.curQ = 0;
    s.curEv = -1;
    s.curLoops = 0;
    s.curFresh = true;
    s.next = kInvalidIndex;
  } else {
    s.next = static_cast<u32>(t.toState);
    s.nextQ = 0;
    s.nextEv = -1;
    s.nextLoops = 0;
    s.nextFresh = true;
    s.transTotal = t.durationTicks;
    s.transElapsed = 0;
  }
  const PackAnimCondition* cs = g_content.conditions(*s.ctl);
  const PackAnimParam* ps = g_content.params(*s.ctl);
  for (u32 k = 0; k < t.conditionCount; ++k) {  // a trigger is consumed by the transition it fires
    const PackAnimCondition& c = cs[t.firstCondition + k];
    if (c.param < kMaxParams && ps[c.param].type == static_cast<u32>(PackAnimParamType::Trigger)) s.params[c.param] = 0;
  }
}

void finishTransition(Slot& s) {
  s.cur = s.next;
  s.curQ = s.nextQ;
  s.curEv = s.nextEv;
  s.curLoops = s.nextLoops;
  s.curFresh = s.nextFresh;
  s.next = kInvalidIndex;
  s.transTotal = s.transElapsed = 0;
}

void chooseTransition(Slot& s, u32 hz) {
  Motion mc;
  motionOf(s, s.cur, hz, mc);
  const Scalar norm = normalized(mc, s.curQ, s.curLoops);
  const PackAnimTransition* ts = g_content.transitions(*s.ctl);
  const PackAnimCondition* cs = g_content.conditions(*s.ctl);
  const PackAnimState& st = statesOf(s)[s.cur];
  auto passes = [&](const PackAnimTransition& t) {
    if ((t.flags & kAnimTransHasExitTime) && norm < t.exitTime) return false;
    for (u32 k = 0; k < t.conditionCount; ++k)
      if (!condTrue(s, cs[t.firstCondition + k])) return false;
    return true;
  };
  for (u32 i = 0; i < st.transitionCount; ++i) {
    const PackAnimTransition& t = ts[st.firstTransition + i];
    if (passes(t)) { transitionStart(s, t); return; }
  }
  for (u32 i = 0; i < s.ctl->anyCount; ++i) {
    const PackAnimTransition& t = ts[s.ctl->anyFirst + i];
    if (static_cast<u32>(t.toState) == s.cur) continue;
    if (passes(t)) { transitionStart(s, t); return; }
  }
}

void stepSlot(World& world, Slot& s, Animator& a, u32 hz) {
  GameObject* go = a.gameObject();
  if (!go) return;
  if (s.next == kInvalidIndex) chooseTransition(s, hz);
  const Scalar asp = a.speed;
  Motion mc, mn;
  motionOf(s, s.cur, hz, mc);
  const bool transitioning = s.next != kInvalidIndex;
  if (s.curFresh) {
    s.curFresh = false;
    advanceState(mc, stateLoops(s, s.cur), 0, s.curQ, s.curEv, s.curLoops, go, !transitioning);
  } else {
    advanceState(mc, stateLoops(s, s.cur), mc.mulFps * speedQ16(s, s.cur, asp), s.curQ, s.curEv, s.curLoops, go, !transitioning);
  }
  Scalar w = 0;
  if (transitioning) {
    motionOf(s, s.next, hz, mn);
    if (s.nextFresh) {
      s.nextFresh = false;
      advanceState(mn, stateLoops(s, s.next), 0, s.nextQ, s.nextEv, s.nextLoops, go, true);
    } else {
      advanceState(mn, stateLoops(s, s.next), mn.mulFps * speedQ16(s, s.next, asp), s.nextQ, s.nextEv, s.nextLoops, go, true);
    }
    ++s.transElapsed;
    w = static_cast<Scalar>(s.transElapsed) / static_cast<Scalar>(s.transTotal);
    if (s.transElapsed >= s.transTotal) {
      finishTransition(s);
      mc = mn;
      mn = Motion();
      w = 0;
    }
  }
  const bool blendNow = s.next != kInvalidIndex;
  applyProps(world, go, mc, s.curQ, 1.0f);
  if (blendNow) applyProps(world, go, mn, s.nextQ, w);
  if (mc.kind == PackAnimStateKind::Clip3 || (blendNow && mn.kind == PackAnimStateKind::Clip3))
    applySkeleton(s, &mc, blendNow ? &mn : nullptr, hz, w);
  g_hash = fold(g_hash, s.cur);
  g_hash = fold(g_hash, s.next);
  g_hash = fold(g_hash, static_cast<u32>(s.curQ));
  g_hash = fold(g_hash, static_cast<u32>(s.curQ >> 32));
  g_hash = fold(g_hash, s.curLoops);
  g_hash = fold(g_hash, s.transElapsed);
  const u32 np = s.ctl->paramCount < kMaxParams ? s.ctl->paramCount : kMaxParams;
  for (u32 i = 0; i < np; ++i) g_hash = fold(g_hash, s.params[i]);
}

}  // namespace

bool addStepHook(StepHook hook) {
  if (!hook) return false;
  for (u32 i = 0; i < g_hookCount; ++i)
    if (g_hooks[i] == hook) return true;
  if (g_hookCount >= kMaxHooks) return false;
  g_hooks[g_hookCount++] = hook;
  return true;
}

void fixedStep(World& world) {
  const u32 hz = tickHz();
  g_hash = 2166136261u;
  bool any = false;
  if (bindPack()) {
    for (Slot& s : g_slots) s.seen = false;
    world.forEach<Animator>([&](Animator& a) {
      GameObject* go = a.gameObject();
      if (!go || !go->activeInHierarchy() || !a.enabled()) {
        if (a.runtimeIndex_ < kMaxAnimators && g_slots[a.runtimeIndex_].owner == &a) g_slots[a.runtimeIndex_].seen = true;
        return;
      }
      Slot* s = slotOf(a, true);
      if (!s) return;
      s->seen = true;
      any = true;
      stepSlot(world, *s, a, hz);
    });
    bool anyPal = false;
    for (Slot& s : g_slots) {
      if (s.used && !s.seen) s = Slot();
      anyPal = anyPal || (s.used && s.hasPal);
    }
    if (!anyPal) g_palUsed = 0;
  } else if (g_anyUsed) {
    g_anyUsed = false;
    for (Slot& s : g_slots) s = Slot();  // a pack without controllers: nothing to keep (slots of a destroyed world)
    g_palUsed = 0;
  }
  for (u32 i = 0; i < g_hookCount; ++i) g_hooks[i](world);
  ++g_steps;
  if (any && g_trace) RLOG_INFO("game", "rensga: anim f=%u h=%08x", static_cast<unsigned>(g_steps - 1), static_cast<unsigned>(g_hash));
}

void shutdown() {
  for (Slot& s : g_slots) s = Slot();
  g_palUsed = 0;
  g_view = nullptr;
  g_content = PackContent();
  g_strs = nullptr;
  g_strsSize = 0;
  g_steps = 0;
  g_hash = 0;
  g_warnedSlots = g_warnedPal = false;
}

void setTrace(bool on) { g_trace = on; }
u32 stepCount() { return g_steps; }
u32 stateHash() { return g_hash; }

bool debugInfo(const Animator& a, DebugInfo& out) {
  out = DebugInfo();
  Slot* s = slotOfConst(a);
  if (!s) return false;
  out.valid = true;
  out.current = s->cur;
  out.next = s->next;
  out.stateCount = s->ctl->stateCount;
  Motion m;
  motionOf(*s, s->cur, tickHz(), m);
  out.normalizedTime = normalized(m, s->curQ, s->curLoops);
  out.blend = s->next != kInvalidIndex && s->transTotal ? static_cast<Scalar>(s->transElapsed) / static_cast<Scalar>(s->transTotal) : 0;
  return true;
}

const Matrix4* palette(const World& world, const SkinnedMeshRenderer& smr, u32* count) {
  if (count) *count = 0;
  GameObject* go = smr.gameObject();
  if (!go) return nullptr;
  const Animator* a = world.getComponent<Animator>(go);
  if (!a || a->runtimeIndex_ >= kMaxAnimators) return nullptr;
  const Slot& s = g_slots[a->runtimeIndex_];
  if (!s.used || s.owner != a || !s.hasPal) return nullptr;
  if (count) *count = s.palCount;
  return g_pal + s.palOffset;
}

}  // namespace AnimatorSystem

// ---- Animator component methods (Unity's names) -----------------------------------------------------------------------------

namespace {
using AnimatorSystem::Slot;
// Param access for the component methods. Types follow Unity: Float, Int, Bool, Trigger; a Float or Int parameter converts.
bool withParam(Animator& a, u32 hash, bool create, u32* bitsOut, u32* typeOut, u32** ref) {
  Slot* s = AnimatorSystem::slotOf(a, create);
  if (!s) return false;
  const i32 i = AnimatorSystem::findParam(*s, hash);
  if (i < 0) return false;
  *typeOut = AnimatorSystem::g_content.params(*s->ctl)[i].type;
  *ref = &s->params[i];
  *bitsOut = s->params[i];
  return true;
}
}  // namespace

void Animator::SetFloat(u32 h, Scalar v) {
  u32 bits, type, *ref;
  if (!withParam(*this, h, true, &bits, &type, &ref)) return;
  if (type == static_cast<u32>(PackAnimParamType::Float)) *ref = AnimatorSystem::floatToBits(v);
  else if (type == static_cast<u32>(PackAnimParamType::Int)) *ref = static_cast<u32>(static_cast<i32>(v));
}
void Animator::SetInt(u32 h, i32 v) {
  u32 bits, type, *ref;
  if (!withParam(*this, h, true, &bits, &type, &ref)) return;
  if (type == static_cast<u32>(PackAnimParamType::Int)) *ref = static_cast<u32>(v);
  else if (type == static_cast<u32>(PackAnimParamType::Float)) *ref = AnimatorSystem::floatToBits(static_cast<Scalar>(v));
}
void Animator::SetBool(u32 h, bool v) {
  u32 bits, type, *ref;
  if (!withParam(*this, h, true, &bits, &type, &ref)) return;
  if (type == static_cast<u32>(PackAnimParamType::Bool) || type == static_cast<u32>(PackAnimParamType::Trigger)) *ref = v ? 1u : 0u;
}
void Animator::SetTrigger(u32 h) {
  u32 bits, type, *ref;
  if (!withParam(*this, h, true, &bits, &type, &ref)) return;
  if (type == static_cast<u32>(PackAnimParamType::Trigger) || type == static_cast<u32>(PackAnimParamType::Bool)) *ref = 1u;
}
void Animator::ResetTrigger(u32 h) {
  u32 bits, type, *ref;
  if (!withParam(*this, h, true, &bits, &type, &ref)) return;
  if (type == static_cast<u32>(PackAnimParamType::Trigger) || type == static_cast<u32>(PackAnimParamType::Bool)) *ref = 0u;
}
void Animator::SetFloat(const char* n, Scalar v) { SetFloat(StringToHash(n), v); }
void Animator::SetInt(const char* n, i32 v) { SetInt(StringToHash(n), v); }
void Animator::SetBool(const char* n, bool v) { SetBool(StringToHash(n), v); }
void Animator::SetTrigger(const char* n) { SetTrigger(StringToHash(n)); }
void Animator::ResetTrigger(const char* n) { ResetTrigger(StringToHash(n)); }

Scalar Animator::GetFloat(const char* n) const {
  u32 bits, type, *ref;
  if (!withParam(const_cast<Animator&>(*this), StringToHash(n), false, &bits, &type, &ref)) return 0;
  if (type == static_cast<u32>(PackAnimParamType::Float)) return AnimatorSystem::bitsToFloat(bits);
  if (type == static_cast<u32>(PackAnimParamType::Int)) return static_cast<Scalar>(static_cast<i32>(bits));
  return 0;
}
i32 Animator::GetInt(const char* n) const {
  u32 bits, type, *ref;
  if (!withParam(const_cast<Animator&>(*this), StringToHash(n), false, &bits, &type, &ref)) return 0;
  if (type == static_cast<u32>(PackAnimParamType::Int)) return static_cast<i32>(bits);
  if (type == static_cast<u32>(PackAnimParamType::Float)) return static_cast<i32>(AnimatorSystem::bitsToFloat(bits));
  return 0;
}
bool Animator::GetBool(const char* n) const {
  u32 bits, type, *ref;
  if (!withParam(const_cast<Animator&>(*this), StringToHash(n), false, &bits, &type, &ref)) return false;
  return bits != 0 && type != static_cast<u32>(PackAnimParamType::Float);
}

bool Animator::Play(u32 stateHash) {
  Slot* s = AnimatorSystem::slotOf(*this, true);
  if (!s) return false;
  const i32 idx = AnimatorSystem::findState(*s, stateHash);
  if (idx < 0) return false;
  s->cur = static_cast<u32>(idx);
  s->curQ = 0;
  s->curEv = -1;
  s->curLoops = 0;
  s->curFresh = true;
  s->next = kInvalidIndex;
  s->transTotal = s->transElapsed = 0;
  return true;
}
bool Animator::Play(const char* n) { return Play(StringToHash(n)); }

bool Animator::CrossFade(const char* n, Scalar seconds) {
  Slot* s = AnimatorSystem::slotOf(*this, true);
  if (!s) return false;
  const i32 idx = AnimatorSystem::findState(*s, StringToHash(n));
  if (idx < 0) return false;
  if (s->next != kInvalidIndex) AnimatorSystem::finishTransition(*s);
  const Scalar hz = static_cast<Scalar>(AnimatorSystem::tickHz());
  const u32 ticks = seconds > 0 ? static_cast<u32>(seconds * hz + 0.5f) : 0;
  PackAnimTransition t = {};
  t.toState = idx;
  t.durationTicks = ticks;
  t.fromState = -1;
  AnimatorSystem::transitionStart(*s, t);
  return true;
}

AnimatorStateInfo Animator::GetCurrentStateInfo() const {
  AnimatorStateInfo info;
  Slot* s = AnimatorSystem::slotOf(const_cast<Animator&>(*this), false);
  if (!s) return info;
  const PackAnimState& st = AnimatorSystem::g_content.states(*s->ctl)[s->cur];
  info.nameHash = st.nameHash;
  AnimatorSystem::Motion m;
  const u32 hz = AnimatorSystem::tickHz();
  AnimatorSystem::motionOf(*s, s->cur, hz, m);
  info.normalizedTime = AnimatorSystem::normalized(m, s->curQ, s->curLoops);
  if (m.kind == PackAnimStateKind::PropClip) info.length = static_cast<Scalar>(m.pc.clip->lengthTicks) / static_cast<Scalar>(m.pc.clip->fps);
  else if (m.kind == PackAnimStateKind::Clip3) info.length = static_cast<Scalar>(m.c3->frameCount) / static_cast<Scalar>(m.c3->fps);
  return info;
}

}  // namespace rensga
