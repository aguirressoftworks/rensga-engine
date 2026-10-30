// F3.05: coroutine and Invoke scheduler (fixed pools, no heap after init).
#include "rensga/core/coroutine.h"

#include <new>
#include <string.h>

#include "rensga/core/event.h"
#include "rensga/core/log.h"
#include "rensga/core/memory.h"
#include "rensga/core/reflect_method.h"
#include "rensga/core/time.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {
constexpr Scalar kTimeEpsilon = 1e-4f;  // float drift of repeated 1/60 subtraction
}

bool Coroutine::isRunning() const {
  World* w = World::active();
  return w && w->scheduler().isRunning(*this);
}

template <class T>
static T* allocSlots(Arena& arena, u32 n) {
  T* p = n ? arena.allocateArray<T>(n) : nullptr;
  if (p)
    for (u32 i = 0; i < n; ++i) new (&p[i]) T();
  return p;
}

bool Scheduler::init(World& world, Arena& arena, u32 maxCoroutines, u32 maxInvokes) {
  world_ = &world;
  co_ = allocSlots<CoSlot>(arena, maxCoroutines);
  inv_ = allocSlots<InvokeSlot>(arena, maxInvokes);
  if ((maxCoroutines && !co_) || (maxInvokes && !inv_)) return false;
  allocCo_ = maxCoroutines_ = maxCoroutines;
  allocInv_ = maxInvokes_ = maxInvokes;
  liveCoroutines_ = liveInvokes_ = peakCoroutines_ = 0;
  frame_ = 1;
  fixedSerial_ = 0;
  tickSerial_ = 1;
  return true;
}

bool Scheduler::resize(Arena& arena, u32 maxCoroutines, u32 maxInvokes) {
  if (!world_) return false;
  if (maxCoroutines > allocCo_) {
    CoSlot* n = allocSlots<CoSlot>(arena, maxCoroutines);
    if (!n) return false;
    for (u32 i = 0; i < allocCo_; ++i) n[i] = co_[i];
    co_ = n;
    allocCo_ = maxCoroutines;
  } else if (maxCoroutines < maxCoroutines_) {
    for (u32 i = maxCoroutines; i < maxCoroutines_; ++i)
      if (co_[i].used) return false;
  }
  if (maxInvokes > allocInv_) {
    InvokeSlot* n = allocSlots<InvokeSlot>(arena, maxInvokes);
    if (!n) return false;
    for (u32 i = 0; i < allocInv_; ++i) n[i] = inv_[i];
    inv_ = n;
    allocInv_ = maxInvokes;
  } else if (maxInvokes < maxInvokes_) {
    for (u32 i = maxInvokes; i < maxInvokes_; ++i)
      if (inv_[i].used) return false;
  }
  if (maxCoroutines) maxCoroutines_ = maxCoroutines;
  if (maxInvokes) maxInvokes_ = maxInvokes;
  return true;
}

void Scheduler::shutdown() {
  for (u32 i = 0; i < allocCo_; ++i) co_[i].used = false;
  for (u32 i = 0; i < allocInv_; ++i) inv_[i].used = false;
  liveCoroutines_ = liveInvokes_ = 0;
}

void Scheduler::beginFrame() { ++frame_; }

bool Scheduler::ownerRunnable(const CoSlot& s) const {
  Component* c = s.owner.resolve();
  if (!c) return false;
  GameObject* go = c->gameObject();
  return go && go->activeInHierarchy();
}

void Scheduler::free(u32 index) {
  CoSlot& s = co_[index];
  if (!s.used) return;
  s.used = false;
  ++s.generation;
  s.body = CoroutineBody();
  s.pred = Predicate();
  --liveCoroutines_;
}

Coroutine Scheduler::start(Behaviour* owner, const CoroutineBody& body) {
  ComponentHandle h;
  if (!owner || !world_->handleOf(owner, h)) return Coroutine();
  u32 idx = kInvalidIndex;
  for (u32 i = 0; i < maxCoroutines_; ++i)
    if (!co_[i].used) { idx = i; break; }
  if (idx == kInvalidIndex) {
    RLOG_WARN("coroutine", "coroutine pool full (%u): raise budgets.maxCoroutines in the ConsoleTarget", maxCoroutines_);
    return Coroutine();
  }
  CoSlot& s = co_[idx];
  s.used = true;
  s.owner = h;
  s.ctx = CoroutineContext();
  s.ctx.owner = owner;
  s.body = body;
  s.pred = Predicate();
  s.wait = YieldKind::NextFrame;
  s.remaining = 0;
  s.armedFrame = frame_;
  s.armedFixed = fixedSerial_;
  s.lastTick = tickSerial_;
  ++liveCoroutines_;
  if (liveCoroutines_ > peakCoroutines_) peakCoroutines_ = liveCoroutines_;
  Coroutine c;
  c.id = idx + 1;
  c.generation = s.generation;
  step(idx);  // like Unity: runs up to the first wait inside StartCoroutine
  return c;
}

void Scheduler::step(u32 index) {
  CoSlot& s = co_[index];
  const u32 gen = s.generation;
  s.lastTick = tickSerial_;
  Yield y = s.body(s.ctx);
  if (!s.used || s.generation != gen) return;  // the body stopped itself
  if (y.kind == YieldKind::Done) { free(index); return; }
  s.wait = y.kind;
  s.remaining = y.seconds;
  s.pred = y.pred;
  s.armedFrame = frame_;
  s.armedFixed = fixedSerial_;
  s.lastTick = tickSerial_;
}

bool Scheduler::isRunning(const Coroutine& c) const {
  if (c.id == 0 || c.id > maxCoroutines_) return false;
  const CoSlot& s = co_[c.id - 1];
  return s.used && s.generation == c.generation;
}

void Scheduler::stop(const Coroutine& c) {
  if (isRunning(c)) free(c.id - 1);
}

void Scheduler::stopAll(const Behaviour* owner) {
  ComponentHandle h;
  if (!world_->handleOf(owner, h)) return;
  for (u32 i = 0; i < maxCoroutines_; ++i)
    if (co_[i].used && co_[i].owner == h) free(i);
}

void Scheduler::onObjectDeactivated(GameObject* go) {
  for (u32 i = 0; i < maxCoroutines_; ++i) {
    if (!co_[i].used) continue;
    Component* c = co_[i].owner.resolve();
    GameObject* g = c ? c->gameObject() : nullptr;
    if (!c || g == go || (g && g->transform.IsChildOf(&go->transform))) free(i);
  }
  for (u32 i = 0; i < maxInvokes_; ++i) {
    if (!inv_[i].used) continue;
    Component* c = inv_[i].owner.resolve();
    GameObject* g = c ? c->gameObject() : nullptr;
    if (!c || g == go || (g && g->transform.IsChildOf(&go->transform))) {
      inv_[i].used = false;
      --liveInvokes_;
    }
  }
}

// phase: 0 after Update, 1 after a fixed step, 2 end of frame
void Scheduler::tick(u32 phase) {
  ++tickSerial_;
  if (phase == 1) ++fixedSerial_;
  const Scalar dt = Time::deltaTime();
  const Scalar udt = Time::unscaledDeltaTime();
  for (u32 i = 0; i < maxCoroutines_; ++i) {
    CoSlot& s = co_[i];
    if (!s.used || s.lastTick >= tickSerial_) continue;
    if (!ownerRunnable(s)) { free(i); continue; }
    bool resume = false;
    if (phase == 0) {
      if (s.armedFrame >= frame_) continue;  // armed this frame: first look next frame
      switch (s.wait) {
        case YieldKind::NextFrame: resume = true; break;
        case YieldKind::Seconds: s.remaining -= dt; resume = s.remaining <= kTimeEpsilon; break;
        case YieldKind::SecondsRealtime: s.remaining -= udt; resume = s.remaining <= kTimeEpsilon; break;
        case YieldKind::Until: resume = s.pred(); break;
        case YieldKind::While: resume = !s.pred(); break;
        default: break;
      }
    } else if (phase == 1) {
      resume = s.wait == YieldKind::FixedUpdate && s.armedFixed < fixedSerial_;
    } else {
      resume = s.wait == YieldKind::EndOfFrame;
    }
    if (resume) step(i);
  }
}

void Scheduler::runUpdate() { tick(0); }
void Scheduler::runFixed() { tick(1); }
void Scheduler::runEndOfFrame() { tick(2); }

// ---- Invoke ------------------------------------------------------------------------------------------------------

bool Scheduler::addInvoke(Behaviour* owner, MemberThunk thunk, const u8* ptr, u32 ptrSize, const MethodInfo* method,
                          Scalar delay, Scalar repeat) {
  ComponentHandle h;
  if (!owner || !world_->handleOf(owner, h)) return false;
  for (u32 i = 0; i < maxInvokes_; ++i)
    if (!inv_[i].used) {
      InvokeSlot& s = inv_[i];
      s = InvokeSlot();
      s.used = true;
      s.owner = h;
      s.thunk = thunk;
      s.method = method;
      if (ptr && ptrSize <= sizeof(s.ptr)) { memcpy(s.ptr, ptr, ptrSize); s.ptrSize = static_cast<u8>(ptrSize); }
      s.remaining = delay < 0 ? 0 : delay;
      s.repeat = repeat;
      s.armedFrame = frame_;
      s.serial = ++invokeSerial_;
      ++liveInvokes_;
      return true;
    }
  RLOG_WARN("invoke", "Invoke table full (%u): raise the invoke capacity", maxInvokes_);
  return false;
}

bool Scheduler::sameTarget(const InvokeSlot& s, const u8* ptr, u32 ptrSize, const MethodInfo* method) const {
  if (method && s.method) return method == s.method;
  return ptrSize && s.ptrSize == ptrSize && memcmp(s.ptr, ptr, ptrSize) == 0;
}

void Scheduler::cancelInvoke(const Behaviour* owner, const u8* ptr, u32 ptrSize, const MethodInfo* method, bool all) {
  ComponentHandle h;
  if (!world_->handleOf(owner, h)) return;
  for (u32 i = 0; i < maxInvokes_; ++i) {
    InvokeSlot& s = inv_[i];
    if (s.used && s.owner == h && (all || sameTarget(s, ptr, ptrSize, method))) {
      s.used = false;
      --liveInvokes_;
    }
  }
}

bool Scheduler::isInvoking(const Behaviour* owner, const u8* ptr, u32 ptrSize, const MethodInfo* method, bool any) const {
  ComponentHandle h;
  if (!world_->handleOf(owner, h)) return false;
  for (u32 i = 0; i < maxInvokes_; ++i) {
    const InvokeSlot& s = inv_[i];
    if (s.used && s.owner == h && (any || sameTarget(s, ptr, ptrSize, method))) return true;
  }
  return false;
}

void Scheduler::runInvokes() {
  const Scalar dt = Time::deltaTime();
  for (u32 i = 0; i < maxInvokes_; ++i) {
    InvokeSlot& s = inv_[i];
    if (!s.used) continue;
    Component* c = s.owner.resolve();
    GameObject* go = c ? c->gameObject() : nullptr;
    if (!c || !go || !go->activeInHierarchy()) { s.used = false; --liveInvokes_; continue; }
    if (s.armedFrame >= frame_) continue;
    s.remaining -= dt;
    for (int guard = 0; guard < 8 && s.used && s.remaining <= kTimeEpsilon; ++guard) {
      const u32 sid = s.serial;
      MemberThunk thunk = s.thunk;
      const MethodInfo* method = s.method;
      u8 ptr[16];
      memcpy(ptr, s.ptr, sizeof(ptr));
      if (s.repeat > 0) {
        s.remaining += s.repeat;
      } else {
        s.used = false;
        --liveInvokes_;
      }
      if (thunk) thunk(static_cast<Behaviour*>(c), ptr);
      else if (method) method->invoke(c, MethodArg());
      if (!s.used || s.serial != sid) break;
      if (!s.owner.alive()) { s.used = false; --liveInvokes_; break; }
    }
  }
}

// ---- Behaviour entry points --------------------------------------------------------------------------------------

namespace detail {

Coroutine startCoroutine(Behaviour* owner, const CoroutineBody& body) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  return w ? w->scheduler().start(owner, body) : Coroutine();
}
void stopCoroutine(const Coroutine& c, Behaviour* owner) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  if (w) w->scheduler().stop(c);
}
void stopAllCoroutines(Behaviour* owner) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  if (w) w->scheduler().stopAll(owner);
}

static const MethodInfo* methodOfPointer(const Behaviour* owner, const u8* ptr, u32 ptrSize) {
  const TypeInfo* t = TypeRegistry::instance().byId(owner->typeId());
  return MethodRegistry::instance().findByPointer(t, ptr, ptrSize);
}
static const MethodInfo* methodByName(const Behaviour* owner, const char* name) {
  const TypeInfo* t = TypeRegistry::instance().byId(owner->typeId());
  return t ? MethodRegistry::instance().find(*t, name) : nullptr;
}

bool invoke(Behaviour* owner, Scheduler::MemberThunk thunk, const u8* ptr, u32 ptrSize, Scalar delay, Scalar repeat) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  return w && w->scheduler().addInvoke(owner, thunk, ptr, ptrSize, methodOfPointer(owner, ptr, ptrSize), delay, repeat);
}
bool invokeByName(Behaviour* owner, const char* name, Scalar delay, Scalar repeat) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  if (!w) return false;
  const MethodInfo* m = methodByName(owner, name);
  if (!m || m->arg != MethodArgKind::None) {
    RLOG_WARN("invoke", "Invoke(\"%s\"): no void() METHOD(%s) on this Behaviour", name, name);
    return false;
  }
  return w->scheduler().addInvoke(owner, nullptr, nullptr, 0, m, delay, repeat);
}
void cancelInvoke(Behaviour* owner, const u8* ptr, u32 ptrSize) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  if (w) w->scheduler().cancelInvoke(owner, ptr, ptrSize, methodOfPointer(owner, ptr, ptrSize), false);
}
void cancelInvokeByName(Behaviour* owner, const char* name) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  const MethodInfo* m = w ? methodByName(owner, name) : nullptr;
  if (m) w->scheduler().cancelInvoke(owner, nullptr, 0, m, false);
}
void cancelAllInvokes(Behaviour* owner) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  if (w) w->scheduler().cancelInvoke(owner, nullptr, 0, nullptr, true);
}
bool isInvoking(const Behaviour* owner, const u8* ptr, u32 ptrSize) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  return w && w->scheduler().isInvoking(owner, ptr, ptrSize, methodOfPointer(owner, ptr, ptrSize), false);
}
bool isInvokingByName(const Behaviour* owner, const char* name) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  const MethodInfo* m = w ? methodByName(owner, name) : nullptr;
  return m && w->scheduler().isInvoking(owner, nullptr, 0, m, false);
}
bool isInvokingAny(const Behaviour* owner) {
  World* w = owner ? owner->ownerWorld() : nullptr;
  return w && w->scheduler().isInvoking(owner, nullptr, 0, nullptr, true);
}

void componentSlotAndGeneration(const Component* c, ComponentHandle& out) {
  if (c && c->ownerWorld()) c->ownerWorld()->handleOf(c, out);
}

}  // namespace detail

}  // namespace rensga
