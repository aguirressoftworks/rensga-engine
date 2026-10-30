// F3.05: a callable stored inline (no heap, RULES 3). Only trivially copyable callables fit: a lambda that captures
// `this` and a few numbers. A callable that is too big or not trivially copyable is a compile error.
#pragma once
#include <new>
#include <type_traits>

#include "rensga/core/types.h"

namespace rensga {

template <class Sig, size_t N>
class InlineFn;

template <class R, class... A, size_t N>
class InlineFn<R(A...), N> {
 public:
  InlineFn() = default;
  template <class F, class = typename std::enable_if<!std::is_same<typename std::decay<F>::type, InlineFn>::value>::type>
  InlineFn(F f) {
    static_assert(sizeof(F) <= N, "callable too big for the inline buffer: capture less (this + a few numbers)");
    static_assert(alignof(F) <= 8, "callable alignment above 8 is not supported");
    static_assert(std::is_trivially_copyable<F>::value, "callable must be trivially copyable (no heap, no destructor)");
    new (buf_) F(f);
    call_ = [](void* p, A... a) -> R { return (*static_cast<F*>(p))(static_cast<A&&>(a)...); };
  }
  explicit operator bool() const { return call_ != nullptr; }
  R operator()(A... a) { return call_(buf_, static_cast<A&&>(a)...); }

 private:
  alignas(8) unsigned char buf_[N] = {};
  R (*call_)(void*, A...) = nullptr;
};

}  // namespace rensga
