// MVP-12: generational handle. Scripts hold Ref<T>, never raw engine pointers (RULES 4).
// A Ref reads as null after the target is destroyed, even if its slot is reused.
#pragma once
#include "rensga/core/types.h"

namespace rensga {

class World;

template <class T>
class Ref {
 public:
  Ref() {}
  Ref(World* world, u32 index, u32 generation) : world_(world), index_(index), generation_(generation) {}

  T* get() const;  // defined in world.h
  T* operator->() const { return get(); }
  T& operator*() const { return *get(); }
  explicit operator bool() const { return get() != nullptr; }
  bool isNull() const { return get() == nullptr; }
  bool operator==(const Ref& o) const {
    return world_ == o.world_ && index_ == o.index_ && generation_ == o.generation_;
  }
  bool operator!=(const Ref& o) const { return !(*this == o); }
  void reset() { *this = Ref(); }

 private:
  World* world_ = nullptr;
  u32 index_ = kInvalidIndex;
  u32 generation_ = 0;
};

}  // namespace rensga
