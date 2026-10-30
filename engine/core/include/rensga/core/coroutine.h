// F3.05 (API 4): coroutines, Invoke. C++17 has no yield, so a coroutine is a small state object the loop resumes:
//
//   // 1. chain
//   StartCoroutine(Wait(1).Then([this] { Open(); }).WaitUntil([this] { return opened; }).Then([this] { Done(); }));
//   // 2. step function: a lambda and a step counter; it returns what to wait for next
//   StartCoroutine([this](CoroutineContext& co) -> Yield {
//     switch (co.step++) {
//       case 0: return WaitForSeconds(0.5f);
//       case 1: Fire(); return YieldNull();      // resumes next frame
//       default: return YieldBreak();
//     }
//   });
//
// The first step runs inside StartCoroutine (like Unity). Waits: WaitForSeconds (scaled time),
// WaitForSecondsRealtime, WaitUntil, WaitWhile, WaitForFixedUpdate, WaitForEndOfFrame, YieldNull (next frame).
// Resume points in the loop: after Update (seconds, until, while, next frame), after each FixedUpdate step
// (WaitForFixedUpdate), after LateUpdate (WaitForEndOfFrame). A coroutine stops by itself when its Behaviour is
// destroyed or its GameObject is deactivated (it does not resume on reactivation, like Unity).
//
// No heap: coroutines live in a fixed pool sized by ConsoleTarget budgets.maxCoroutines (default 64); the callable
// is stored inline (trivially copyable, small: capture `this` and a few numbers, enforced by static_assert).
#pragma once
#include <type_traits>

#include "rensga/core/component_handle.h"
#include "rensga/core/inline_fn.h"
#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {

class Arena;
class Behaviour;
class World;
struct GameObject;
struct MethodInfo;

constexpr u32 kDefaultMaxCoroutines = 64;
constexpr u32 kDefaultMaxInvokes = 64;
constexpr u32 kCoroutineBodyBytes = 208;
constexpr u32 kPredicateBytes = 24;
constexpr u32 kMaxChainSteps = 6;

using Predicate = InlineFn<bool(), kPredicateBytes>;

enum class YieldKind : u8 { Done, NextFrame, Seconds, SecondsRealtime, Until, While, FixedUpdate, EndOfFrame };

// What a coroutine waits for next.
struct Yield {
  YieldKind kind = YieldKind::Done;
  Scalar seconds = 0;
  Predicate pred;
};

inline Yield YieldBreak() { return Yield(); }
inline Yield YieldNull() { Yield y; y.kind = YieldKind::NextFrame; return y; }
inline Yield WaitForSeconds(Scalar s) { Yield y; y.kind = YieldKind::Seconds; y.seconds = s; return y; }
inline Yield WaitForSecondsRealtime(Scalar s) { Yield y; y.kind = YieldKind::SecondsRealtime; y.seconds = s; return y; }
inline Yield WaitForFixedUpdate() { Yield y; y.kind = YieldKind::FixedUpdate; return y; }
inline Yield WaitForEndOfFrame() { Yield y; y.kind = YieldKind::EndOfFrame; return y; }
template <class F>
Yield WaitUntil(F f) { Yield y; y.kind = YieldKind::Until; y.pred = Predicate(f); return y; }
template <class F>
Yield WaitWhile(F f) { Yield y; y.kind = YieldKind::While; y.pred = Predicate(f); return y; }

// Passed to a step-function coroutine. `step` starts at 0; the function advances it (co.step++ in a switch).
struct CoroutineContext {
  u32 step = 0;
  Behaviour* owner = nullptr;
};

// Handle returned by StartCoroutine. Reads as not running once the coroutine finished or was stopped.
struct Coroutine {
  u32 id = 0;  // slot + 1; 0 = none
  u32 generation = 0;
  bool isValid() const { return id != 0; }
  bool isRunning() const;
};

using CoroutineBody = InlineFn<Yield(CoroutineContext&), kCoroutineBodyBytes>;

// Chain builder: Wait(1).Then(f).WaitUntil(p).Then(g). Up to kMaxChainSteps steps; callables up to 16 bytes.
class Chain {
 public:
  using Fn = InlineFn<bool(), 16>;
  struct Step {
    YieldKind kind = YieldKind::Done;  // Done = a Then(callable) step
    Scalar seconds = 0;
    Fn fn;
  };

  Chain& Wait(Scalar s) { return add(YieldKind::Seconds, s, Fn()); }
  Chain& WaitRealtime(Scalar s) { return add(YieldKind::SecondsRealtime, s, Fn()); }
  Chain& NextFrame() { return add(YieldKind::NextFrame, 0, Fn()); }
  Chain& WaitFixedUpdate() { return add(YieldKind::FixedUpdate, 0, Fn()); }
  Chain& WaitEndOfFrame() { return add(YieldKind::EndOfFrame, 0, Fn()); }
  template <class F>
  Chain& Then(F f) {
    return add(YieldKind::Done, 0, Fn([f]() mutable { f(); return true; }));
  }
  template <class F>
  Chain& WaitUntil(F f) { return add(YieldKind::Until, 0, Fn(f)); }
  template <class F>
  Chain& WaitWhile(F f) { return add(YieldKind::While, 0, Fn(f)); }

  bool overflowed() const { return overflow_; }
  u32 size() const { return count_; }

  Yield operator()(CoroutineContext& co) {
    while (co.step < count_) {
      Step& s = steps_[co.step++];
      Fn fn = s.fn;
      switch (s.kind) {
        case YieldKind::Done: fn(); break;
        case YieldKind::Seconds: return WaitForSeconds(s.seconds);
        case YieldKind::SecondsRealtime: return WaitForSecondsRealtime(s.seconds);
        case YieldKind::NextFrame: return YieldNull();
        case YieldKind::FixedUpdate: return WaitForFixedUpdate();
        case YieldKind::EndOfFrame: return WaitForEndOfFrame();
        case YieldKind::Until: { Yield y; y.kind = YieldKind::Until; y.pred = Predicate([fn]() mutable { return fn(); }); return y; }
        case YieldKind::While: { Yield y; y.kind = YieldKind::While; y.pred = Predicate([fn]() mutable { return fn(); }); return y; }
      }
    }
    return YieldBreak();
  }

 private:
  Chain& add(YieldKind k, Scalar s, Fn fn) {
    if (count_ >= kMaxChainSteps) { overflow_ = true; return *this; }
    steps_[count_].kind = k;
    steps_[count_].seconds = s;
    steps_[count_].fn = fn;
    ++count_;
    return *this;
  }
  Step steps_[kMaxChainSteps];
  u32 count_ = 0;
  bool overflow_ = false;
};
static_assert(std::is_trivially_copyable<Chain>::value, "Chain is stored inline");
static_assert(sizeof(Chain) <= kCoroutineBodyBytes, "Chain must fit the coroutine body buffer");

// Starts a chain with a wait. Unity-style: StartCoroutine(Wait(1).Then(...)).
inline Chain Wait(Scalar seconds) { Chain c; c.Wait(seconds); return c; }

// ---- scheduler (one per World) -----------------------------------------------------------------------------------

class Scheduler {
 public:
  bool init(World& world, Arena& arena, u32 maxCoroutines, u32 maxInvokes);
  // Grows or shrinks the pools (pack load). Only when nothing runs; returns false (keeps the old pools) otherwise.
  bool resize(Arena& arena, u32 maxCoroutines, u32 maxInvokes);
  void shutdown();

  // ---- loop hooks (GameLoop::frame)
  void beginFrame();            // frame counter for the "not in the frame it was armed" rule
  void runInvokes();            // Invoke / InvokeRepeating timers (scaled time), before Update
  void runUpdate();             // after Update: next frame, seconds, until, while
  void runFixed();              // after each FixedUpdate step: WaitForFixedUpdate
  void runEndOfFrame();         // after LateUpdate: WaitForEndOfFrame

  Coroutine start(Behaviour* owner, const CoroutineBody& body);
  bool isRunning(const Coroutine& c) const;
  void stop(const Coroutine& c);
  void stopAll(const Behaviour* owner);
  void onObjectDeactivated(GameObject* go);  // stops coroutines owned by components of `go` and its children
  u32 coroutineCount() const { return liveCoroutines_; }
  u32 coroutineCapacity() const { return maxCoroutines_; }
  u32 coroutinePeak() const { return peakCoroutines_; }

  // ---- Invoke
  using MemberThunk = void (*)(Behaviour* self, const u8* ptr);
  bool addInvoke(Behaviour* owner, MemberThunk thunk, const u8* ptr, u32 ptrSize, const MethodInfo* method,
                 Scalar delay, Scalar repeat);
  void cancelInvoke(const Behaviour* owner, const u8* ptr, u32 ptrSize, const MethodInfo* method, bool all);
  bool isInvoking(const Behaviour* owner, const u8* ptr, u32 ptrSize, const MethodInfo* method, bool any) const;
  u32 invokeCount() const { return liveInvokes_; }

 private:
  struct CoSlot {
    u32 generation = 1;
    bool used = false;
    YieldKind wait = YieldKind::Done;
    Scalar remaining = 0;
    u32 armedFrame = 0;   // frame the slot last yielded in
    u32 lastTick = 0;     // tick serial the slot last ran in
    u32 armedFixed = 0;
    ComponentHandle owner;
    CoroutineContext ctx;
    Predicate pred;
    CoroutineBody body;
  };
  struct InvokeSlot {
    bool used = false;
    ComponentHandle owner;
    MemberThunk thunk = nullptr;
    const MethodInfo* method = nullptr;
    u8 ptr[16] = {};
    u8 ptrSize = 0;
    Scalar remaining = 0;
    Scalar repeat = 0;  // <= 0: once
    u32 armedFrame = 0;
    u32 serial = 0;  // identifies this Invoke while its slot may be reused
  };
  void step(u32 index);                     // runs the body until the next wait
  void free(u32 index);
  bool ownerRunnable(const CoSlot& s) const;
  bool sameTarget(const InvokeSlot& s, const u8* ptr, u32 ptrSize, const MethodInfo* method) const;
  void tick(u32 phase);

  World* world_ = nullptr;
  CoSlot* co_ = nullptr;
  InvokeSlot* inv_ = nullptr;
  u32 allocCo_ = 0, allocInv_ = 0;      // allocated capacity
  u32 maxCoroutines_ = 0, maxInvokes_ = 0;  // logical capacity (ConsoleTarget)
  u32 liveCoroutines_ = 0, liveInvokes_ = 0, peakCoroutines_ = 0;
  u32 frame_ = 1;
  u32 fixedSerial_ = 0;
  u32 tickSerial_ = 1;
  u32 invokeSerial_ = 0;
};

namespace detail {
Coroutine startCoroutine(Behaviour* owner, const CoroutineBody& body);
void stopCoroutine(const Coroutine& c, Behaviour* owner);
void stopAllCoroutines(Behaviour* owner);
bool invoke(Behaviour* owner, Scheduler::MemberThunk thunk, const u8* ptr, u32 ptrSize, Scalar delay, Scalar repeat);
bool invokeByName(Behaviour* owner, const char* name, Scalar delay, Scalar repeat);
void cancelInvoke(Behaviour* owner, const u8* ptr, u32 ptrSize);
void cancelInvokeByName(Behaviour* owner, const char* name);
void cancelAllInvokes(Behaviour* owner);
bool isInvoking(const Behaviour* owner, const u8* ptr, u32 ptrSize);
bool isInvokingByName(const Behaviour* owner, const char* name);
bool isInvokingAny(const Behaviour* owner);

template <class F>
struct IsChain : std::false_type {};
template <>
struct IsChain<Chain> : std::true_type {};

template <class T>
void callMember(Behaviour* self, const u8* ptr) {
  void (T::*m)();
  __builtin_memcpy(&m, ptr, sizeof(m));
  (static_cast<T*>(self)->*m)();
}
}  // namespace detail

}  // namespace rensga
