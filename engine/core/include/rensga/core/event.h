// F3.05 (API 4): Event<Args...>, the C# `event` of the script API. Fixed capacity, no heap.
//
//   Event<i32> onScore;                                   // in a class; N handlers (default 8)
//   onScore.Subscribe(&Hud::ShowScore, hud);              // member function + object
//   onScore.Subscribe(&logScore);                         // free function
//   onScore += [](i32 s) { ... };                         // captureless lambda
//   onScore.Invoke(10);   onScore -= &logScore;   onScore.Unsubscribe(&Hud::ShowScore, hud);
//
// Handlers can unsubscribe (themselves or others) while Invoke runs: removed handlers do not run and the list is
// compacted when the outermost Invoke returns. A handler added during Invoke runs from the next Invoke. When the
// handler's object is a Component and it was destroyed, the handler is skipped and removed.
#pragma once
#include <string.h>

#include "rensga/core/component_handle.h"
#include "rensga/core/object.h"

namespace rensga {

template <class... Args>
class EventT {
 public:
  static constexpr u32 kCapacity = 8;
  EventT() = default;

  // free function / captureless lambda
  bool Subscribe(void (*fn)(Args...)) {
    Entry* e = alloc();
    if (!e) return false;
    e->kind = kFree;
    e->call = &callFree;
    e->fn = reinterpret_cast<void (*)()>(fn);
    return true;
  }
  template <class L>
  EventT& operator+=(L lambda) {
    static_assert(std::is_convertible<L, void (*)(Args...)>::value, "+= takes a free function or a captureless lambda; use Subscribe(&T::Method, obj) for members");
    Subscribe(static_cast<void (*)(Args...)>(lambda));
    return *this;
  }
  template <class L>
  EventT& operator-=(L lambda) {
    Unsubscribe(static_cast<void (*)(Args...)>(lambda));
    return *this;
  }
  bool Unsubscribe(void (*fn)(Args...)) {
    for (u32 i = 0; i < count_; ++i)
      if (entries_[i].live && entries_[i].kind == kFree && entries_[i].fn == reinterpret_cast<void (*)()>(fn)) {
        remove(i);
        return true;
      }
    return false;
  }

  // member function + object
  template <class T>
  bool Subscribe(void (T::*method)(Args...), T* object) {
    Entry* e = alloc();
    if (!e) return false;
    e->kind = kMember;
    e->call = &callMember<T>;
    e->object = object;
    memcpy(e->mem, &method, sizeof(method));
    static_assert(sizeof(method) <= sizeof(e->mem), "member function pointer too large");
    e->memSize = sizeof(method);
    if constexpr (std::is_base_of<Component, T>::value) {
      Component* c = static_cast<Component*>(object);
      if (c && c->ownerWorld()) {
        e->owner.world = c->ownerWorld();
        e->owner.typeId = c->typeId();
        e->hasOwner = true;
        bindOwner(e, c);
      }
    }
    return true;
  }
  template <class T>
  bool Unsubscribe(void (T::*method)(Args...), T* object) {
    u8 b[16] = {};
    memcpy(b, &method, sizeof(method));
    for (u32 i = 0; i < count_; ++i)
      if (entries_[i].live && entries_[i].kind == kMember && entries_[i].object == object &&
          memcmp(entries_[i].mem, b, 16) == 0 && entries_[i].call == &callMember<T>) {
        remove(i);
        return true;
      }
    return false;
  }

  void Clear() {
    for (u32 i = 0; i < count_; ++i) entries_[i].live = false;
    if (depth_ == 0) count_ = 0;
  }
  u32 Count() const {
    u32 n = 0;
    for (u32 i = 0; i < count_; ++i) n += entries_[i].live ? 1 : 0;
    return n;
  }

  void Invoke(Args... args) {
    ++depth_;
    const u32 n = count_;  // handlers added by a handler run next time
    for (u32 i = 0; i < n; ++i) {
      Entry& e = entries_[i];
      if (!e.live) continue;
      if (e.hasOwner && !ownerAlive(e)) { e.live = false; continue; }
      e.call(e, static_cast<Args&&>(args)...);
    }
    if (--depth_ == 0) compact();
  }
  void operator()(Args... args) { Invoke(static_cast<Args&&>(args)...); }

 private:
  enum Kind : u8 { kFree, kMember };
  struct Entry {
    bool live = false;
    bool hasOwner = false;
    Kind kind = kFree;
    u8 memSize = 0;
    void (*fn)() = nullptr;
    void* object = nullptr;
    u8 mem[16] = {};
    void (*call)(Entry&, Args...) = nullptr;
    ComponentHandle owner;
  };
  static void callFree(Entry& e, Args... a) { reinterpret_cast<void (*)(Args...)>(e.fn)(static_cast<Args&&>(a)...); }
  template <class T>
  static void callMember(Entry& e, Args... a) {
    void (T::*m)(Args...);
    memcpy(&m, e.mem, sizeof(m));
    (static_cast<T*>(e.object)->*m)(static_cast<Args&&>(a)...);
  }
  static bool ownerAlive(const Entry& e) { return e.owner.alive(); }
  static void bindOwner(Entry* e, Component* c);  // defined below (needs World)
  Entry* alloc() {
    if (depth_ == 0) compact();
    if (count_ >= kCapacity) return nullptr;
    Entry* e = &entries_[count_++];
    *e = Entry();
    e->live = true;
    return e;
  }
  void remove(u32 i) {
    entries_[i].live = false;
    if (depth_ == 0) compact();
  }
  void compact() {
    u32 w = 0;
    for (u32 r = 0; r < count_; ++r)
      if (entries_[r].live) {
        if (w != r) entries_[w] = entries_[r];
        ++w;
      }
    count_ = w;
  }
  Entry entries_[kCapacity];
  u32 count_ = 0;
  u32 depth_ = 0;
};

template <class... Args>
using Event = EventT<Args...>;

namespace detail {
// Fills slot and generation of a component's handle (world.h implements it).
void componentSlotAndGeneration(const Component* c, ComponentHandle& out);
}  // namespace detail

template <class... Args>
void EventT<Args...>::bindOwner(Entry* e, Component* c) {
  detail::componentSlotAndGeneration(c, e->owner);
}

}  // namespace rensga
