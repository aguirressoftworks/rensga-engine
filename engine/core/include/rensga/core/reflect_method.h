// F3.05: method registry behind UnityEvent and name-based Invoke("Open", 2).
//
// A script declares a method for the registry with METHOD(Name) inside a REFLECT class (public, not overloaded):
//
//   struct Light : Behaviour {
//     REFLECT(Light)
//     SERIALIZE_FIELD(Scalar, intensity, 1)
//     METHOD(TurnOn)         // void()
//     METHOD(SetIntensity)   // void(Scalar)   also void(i32), void(bool), void(Guid) (an object GUID)
//     void TurnOn();
//     void SetIntensity(Scalar v);
//   };
//
// tools/reflect/reflect_scan.py reads METHOD(...) and the generated table calls reflect::addMethod<T, &T::Name>.
// The editor lists methods with methodsOf(type); the runtime calls them through MethodInfo::invoke.
// Registry storage is fixed (kMaxMethods); it never allocates.
#pragma once
#include <string.h>

#include <vector>

#include "rensga/core/guid.h"
#include "rensga/core/reflect.h"
#include "rensga/core/scalar.h"

namespace rensga {

enum class MethodArgKind : u8 { None = 0, Scalar = 1, I32 = 2, Bool = 3, Object = 4 };

// Value handed to a one-argument method. Only the member matching the method's MethodArgKind is read.
struct MethodArg {
  Scalar s = 0;
  i32 i = 0;
  bool b = false;
  Guid g;
};

struct MethodInfo {
  const TypeInfo* type = nullptr;
  const char* name = nullptr;
  u32 hash = 0;  // FNV-1a 32 of name
  MethodArgKind arg = MethodArgKind::None;
  void (*invoke)(void* object, const MethodArg& arg) = nullptr;
  u8 ptr[16] = {};  // the member-function pointer bytes: lets Invoke(&T::M, ...) find the method's name
  u8 ptrSize = 0;
};

constexpr u32 kMaxMethods = 256;

class MethodRegistry {
 public:
  static MethodRegistry& instance();
  // Returns null when the table is full or the name is already registered for the type.
  MethodInfo* add(const TypeInfo* type, const char* name, MethodArgKind arg, void (*invoke)(void*, const MethodArg&));
  const MethodInfo* find(const TypeInfo& type, u32 nameHash) const;
  const MethodInfo* find(const TypeInfo& type, const char* name) const;
  // The method of `type` (or any type) whose member-function pointer bytes match; null if not registered.
  const MethodInfo* findByPointer(const TypeInfo* type, const u8* bytes, u32 size) const;
  u32 methodsOf(const TypeInfo& type, const MethodInfo** out, u32 cap) const;
  void forgetType(const TypeInfo* type);
  u32 count() const { return count_; }

 private:
  MethodInfo methods_[kMaxMethods];
  u32 count_ = 0;
};

// Methods declared with METHOD(...) on `type`, in declaration order. Editor use.
inline u32 methodsOf(const TypeInfo& type, const MethodInfo** out, u32 cap) {
  return MethodRegistry::instance().methodsOf(type, out, cap);
}
inline std::vector<const MethodInfo*> methodsOf(const TypeInfo& type) {
  std::vector<const MethodInfo*> v;
  const MethodInfo* tmp[kMaxMethods];
  u32 n = MethodRegistry::instance().methodsOf(type, tmp, kMaxMethods);
  for (u32 i = 0; i < n; ++i) v.push_back(tmp[i]);
  return v;
}
inline const MethodInfo* findMethod(const TypeInfo& type, const char* name) { return MethodRegistry::instance().find(type, name); }
// Drops every method of a type (script hot reload unloads the module that owns it).
inline void forgetType(const TypeInfo* type) { MethodRegistry::instance().forgetType(type); }
// Unregister-hook adapter for TypeRegistry::addUnregisterHook.
void forgetTypeHook(const TypeInfo* type);

namespace reflect {

template <class T> struct MethodArgKindOf { static constexpr bool ok = false; };
template <> struct MethodArgKindOf<float> { static constexpr bool ok = true; static constexpr MethodArgKind kind = MethodArgKind::Scalar; };
template <> struct MethodArgKindOf<i32> { static constexpr bool ok = true; static constexpr MethodArgKind kind = MethodArgKind::I32; };
template <> struct MethodArgKindOf<bool> { static constexpr bool ok = true; static constexpr MethodArgKind kind = MethodArgKind::Bool; };
template <> struct MethodArgKindOf<Guid> { static constexpr bool ok = true; static constexpr MethodArgKind kind = MethodArgKind::Object; };

template <class P> struct MethodTraits { static constexpr bool ok = false; };
template <class C> struct MethodTraits<void (C::*)()> {
  static constexpr bool ok = true;
  static constexpr MethodArgKind kind = MethodArgKind::None;
};
template <class C, class A> struct MethodTraits<void (C::*)(A)> {
  using Plain = typename std::remove_cv<typename std::remove_reference<A>::type>::type;
  static constexpr bool ok = MethodArgKindOf<Plain>::ok;
  static constexpr MethodArgKind kind = MethodArgKindOf<Plain>::kind;
};

template <class C, auto M>
void callMethod(void* object, const MethodArg& a) {
  using Tr = MethodTraits<decltype(M)>;
  C* c = static_cast<C*>(object);
  if constexpr (Tr::kind == MethodArgKind::None) (c->*M)();
  else if constexpr (Tr::kind == MethodArgKind::Scalar) (c->*M)(a.s);
  else if constexpr (Tr::kind == MethodArgKind::I32) (c->*M)(a.i);
  else if constexpr (Tr::kind == MethodArgKind::Bool) (c->*M)(a.b);
  else (c->*M)(a.g);
}

// Used by the generated tables (METHOD(Name) becomes addMethod<T, &T::Name>(ti, "Name")).
template <class C, auto M>
void addMethod(TypeInfo* info, const char* name) {
  using Tr = MethodTraits<decltype(M)>;
  static_assert(Tr::ok, "METHOD(...) needs a public, non-overloaded void() or void(Scalar|i32|bool|Guid) member");
  if (!info) return;
  MethodInfo* m = MethodRegistry::instance().add(info, name, Tr::kind, &callMethod<C, M>);
  if (m) {
    decltype(M) copy = M;
    static_assert(sizeof(copy) <= sizeof(m->ptr), "member function pointer larger than 16 bytes");
    memcpy(m->ptr, &copy, sizeof(copy));
    m->ptrSize = sizeof(copy);
  }
}

}  // namespace reflect
}  // namespace rensga

// Declaration only: the scanner reads it (see reflect_method.h header comment).
#define METHOD(name) static_assert(true, "");
