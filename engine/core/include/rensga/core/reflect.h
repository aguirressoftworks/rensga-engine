// MVP-13: reflection. Fields are declared once; inspector, serialization, cooker and loader read it.
//
//   struct PlayerController : Behaviour {
//     REFLECT(PlayerController)
//     HEADER("Movement")
//     RANGE(0, 20)
//     SERIALIZE_FIELD(Scalar, speed, 5)
//   };
//
// Rules: REFLECT must be the first line in the class body and leaves the class in `public:`.
// HEADER and RANGE apply to the NEXT SERIALIZE_FIELD. Every macro ends with its own ';', so a
// trailing ';' after it is harmless. Registration happens at static init (the F1.05 editor codegen
// replaces registration, not the macros). Reflected types must be default-constructible, hold at
// most kMaxFields fields, and not use multiple inheritance.
#pragma once
#include <new>

#include "rensga/core/guid.h"
#include "rensga/core/math.h"
#include "rensga/core/types.h"

namespace rensga {

class Component;
class Behaviour;

enum class FieldType : u8 { Scalar, I32, Bool, Vector3, Quaternion, Color32, Guid };

// Byte size of a field value in the pack blob (and in memory for all but Bool padding rules).
u32 fieldBlobSize(FieldType t);
const char* fieldTypeName(FieldType t);

struct FieldDesc {
  const char* name = nullptr;
  FieldType type = FieldType::Scalar;
  u32 offset = 0;  // byte offset inside the object
  u32 size = 0;    // sizeof(field)
  bool hasRange = false;
  Scalar rangeMin = 0, rangeMax = 0;
  const char* header = nullptr;
};

constexpr u32 kMaxFields = 32;

struct TypeInfo {
  const char* name = nullptr;
  u32 hash = 0;  // FNV-1a 32 of name
  u32 id = 0;    // index in the registry
  u32 size = 0;
  u32 align = 0;
  FieldDesc* fields = nullptr;
  u32 fieldCount = 0;
  void (*construct)(void* where) = nullptr;
  void (*destruct)(void* obj) = nullptr;
  Component* (*asComponent)(void* obj) = nullptr;  // null if not a Component
  Behaviour* (*asBehaviour)(void* obj) = nullptr;  // null if not a Behaviour
  bool isBehaviour() const { return asBehaviour != nullptr; }
};

constexpr u32 kMaxTypes = 128;

class TypeRegistry {
 public:
  static TypeRegistry& instance();
  const TypeInfo* find(const char* name) const;
  const TypeInfo* findByHash(u32 hash) const;
  const TypeInfo* byId(u32 id) const { return id < count_ ? &types_[id] : nullptr; }
  u32 count() const { return count_; }
  // Internal: used by the macros.
  TypeInfo* add(const char* name, u32 size, u32 align);

 private:
  TypeInfo types_[kMaxTypes];
  u32 count_ = 0;
};

// ---- binary field blobs (pack FDAT) and defaults ---------------------------

// Total size of a type's blob (sum of fieldBlobSize).
u32 blobSize(const TypeInfo& t);
// Writes `obj`'s fields in declaration order. `out` must hold blobSize(t) bytes.
void writeBlob(const TypeInfo& t, const void* obj, u8* out);
// Reads fields into `obj`. Returns false (object untouched) if size != blobSize(t).
bool readBlob(const TypeInfo& t, void* obj, const u8* blob, u32 size);
// True if both objects have equal reflected field values.
bool fieldsEqual(const TypeInfo& t, const void* a, const void* b);

// ---- machinery behind the macros -------------------------------------------

namespace reflect {

template <class T> struct FieldTypeOf;
template <> struct FieldTypeOf<float> { static constexpr FieldType value = FieldType::Scalar; };
template <> struct FieldTypeOf<i32> { static constexpr FieldType value = FieldType::I32; };
template <> struct FieldTypeOf<bool> { static constexpr FieldType value = FieldType::Bool; };
template <> struct FieldTypeOf<Vector3> { static constexpr FieldType value = FieldType::Vector3; };
template <> struct FieldTypeOf<Quaternion> { static constexpr FieldType value = FieldType::Quaternion; };
template <> struct FieldTypeOf<Color32> { static constexpr FieldType value = FieldType::Color32; };
template <> struct FieldTypeOf<Guid> { static constexpr FieldType value = FieldType::Guid; };

struct PendingMods {
  const char* header = nullptr;
  bool hasRange = false;
  Scalar min = 0, max = 0;
};
PendingMods& pending();
inline bool setHeader(const char* text) { pending().header = text; return true; }
inline bool setRange(Scalar lo, Scalar hi) {
  PendingMods& p = pending();
  p.hasRange = true; p.min = lo; p.max = hi;
  return true;
}
// Adds a field to a type under construction (consumes the pending HEADER/RANGE).
void addField(TypeInfo* info, const char* name, FieldType type, u32 offset, u32 size);

template <class C>
struct TypeBuilder {
  static inline TypeInfo* info = nullptr;
  static inline FieldDesc fields[kMaxFields] = {};
};

template <class C, class T>
u32 offsetOfMember(T C::*member) {
  alignas(C) static unsigned char storage[sizeof(C)];
  const C* obj = reinterpret_cast<const C*>(storage);
  return static_cast<u32>(reinterpret_cast<const unsigned char*>(&(obj->*member)) - storage);
}

template <class C>
bool registerType(const char* name) {
  TypeInfo* info = TypeRegistry::instance().add(name, sizeof(C), alignof(C));
  if (!info) return false;
  info->fields = TypeBuilder<C>::fields;
  info->construct = [](void* where) { new (where) C(); };
  info->destruct = [](void* obj) { static_cast<C*>(obj)->~C(); };
  if constexpr (__is_base_of(Component, C)) {
    info->asComponent = [](void* obj) -> Component* { return static_cast<C*>(obj); };
  }
  if constexpr (__is_base_of(Behaviour, C)) {
    info->asBehaviour = [](void* obj) -> Behaviour* { return static_cast<C*>(obj); };
  }
  TypeBuilder<C>::info = info;
  return true;
}

template <class C, class T>
bool registerField(const char* name, T C::*member) {
  addField(TypeBuilder<C>::info, name, FieldTypeOf<T>::value, offsetOfMember<C, T>(member), sizeof(T));
  return true;
}

}  // namespace reflect
}  // namespace rensga

#define REFLECT(Type)                                                                       \
 public:                                                                                    \
  using Self = Type;                                                                        \
  static const ::rensga::TypeInfo* staticTypeInfo() {                                       \
    return ::rensga::reflect::TypeBuilder<Type>::info;                                      \
  }                                                                                         \
  static inline const bool RENSGA_CAT(_rg_type_, Type) = ::rensga::reflect::registerType<Type>(#Type);

#define SERIALIZE_FIELD(type, name, def)                                                        \
  type name = def;                                                                              \
  static inline const bool RENSGA_CAT(_rg_field_, name) =                                       \
      ::rensga::reflect::registerField<Self, type>(#name, &Self::name);

#define RANGE(lo, hi) \
  static inline const bool RENSGA_CAT(_rg_range_, __LINE__) = ::rensga::reflect::setRange(lo, hi);

#define HEADER(text) \
  static inline const bool RENSGA_CAT(_rg_header_, __LINE__) = ::rensga::reflect::setHeader(text);
