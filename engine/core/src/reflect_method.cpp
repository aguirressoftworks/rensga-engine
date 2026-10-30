#include "rensga/core/reflect_method.h"

#include <string.h>

#include "rensga/core/log.h"

namespace rensga {

MethodRegistry& MethodRegistry::instance() {
  static MethodRegistry r;
  // Script hot reload unloads types: drop their methods with them (hook runs for each type before it is cleared).
  static const bool hooked = TypeRegistry::addUnregisterHook(&forgetTypeHook);
  (void)hooked;
  return r;
}

MethodInfo* MethodRegistry::add(const TypeInfo* type, const char* name, MethodArgKind arg,
                                void (*invoke)(void*, const MethodArg&)) {
  if (!type || !name) return nullptr;
  if (find(*type, name)) {
    RLOG_ERROR("reflect", "method '%s.%s' registered twice (METHOD does not support overloads)", type->name, name);
    return nullptr;
  }
  if (count_ >= kMaxMethods) {
    RLOG_ERROR("reflect", "method registry full (%u)", kMaxMethods);
    return nullptr;
  }
  MethodInfo& m = methods_[count_++];
  m = MethodInfo();
  m.type = type;
  m.name = name;
  m.hash = fnv1a32(name);
  m.arg = arg;
  m.invoke = invoke;
  return &m;
}

const MethodInfo* MethodRegistry::find(const TypeInfo& type, u32 nameHash) const {
  for (u32 i = 0; i < count_; ++i)
    if (methods_[i].type == &type && methods_[i].hash == nameHash) return &methods_[i];
  return nullptr;
}

const MethodInfo* MethodRegistry::find(const TypeInfo& type, const char* name) const {
  if (!name) return nullptr;
  for (u32 i = 0; i < count_; ++i)
    if (methods_[i].type == &type && strcmp(methods_[i].name, name) == 0) return &methods_[i];
  return nullptr;
}

const MethodInfo* MethodRegistry::findByPointer(const TypeInfo* type, const u8* bytes, u32 size) const {
  if (!size || size > sizeof(methods_[0].ptr)) return nullptr;
  // Prefer the type's own method; fall back to any type (a method declared in a base class).
  for (int pass = 0; pass < 2; ++pass)
    for (u32 i = 0; i < count_; ++i) {
      const MethodInfo& m = methods_[i];
      if (pass == 0 && m.type != type) continue;
      if (m.ptrSize == size && memcmp(m.ptr, bytes, size) == 0) return &m;
    }
  return nullptr;
}

u32 MethodRegistry::methodsOf(const TypeInfo& type, const MethodInfo** out, u32 cap) const {
  u32 n = 0;
  for (u32 i = 0; i < count_ && n < cap; ++i)
    if (methods_[i].type == &type) out[n++] = &methods_[i];
  return n;
}

void MethodRegistry::forgetType(const TypeInfo* type) {
  u32 w = 0;
  for (u32 r = 0; r < count_; ++r)
    if (methods_[r].type != type) {
      if (w != r) methods_[w] = methods_[r];
      ++w;
    }
  count_ = w;
}

void forgetTypeHook(const TypeInfo* type) { MethodRegistry::instance().forgetType(type); }

}  // namespace rensga
