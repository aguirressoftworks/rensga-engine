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
// HEADER, TOOLTIP, RANGE, SPACE and HIDE_IN_INSPECTOR apply to the NEXT SERIALIZE_FIELD; REQUIRE_COMPONENT(Type) and
// EXECUTE_IN_EDIT_MODE() apply to the class; CONTEXT_MENU("Label") applies to the void() method declared right after it.
// Editor callbacks (Behaviour): OnValidate after an inspector edit, Reset on Add Component / the Reset menu,
// OnDrawGizmos / OnDrawGizmosSelected for the Scene view. The editor runs OnValidate, Reset and CONTEXT_MENU methods on a
// temporary detached instance (gameObject is null there), so they may touch only the component's own fields.
// Every macro ends with its own ';', so a trailing ';' after it is harmless.
//
// F1.05: the macros only declare. tools/reflect/reflect_scan.py reads the source at build time and writes
// <file>.reflect.gen.cpp, which #includes the file and registers the type table (names, offsets, editor metadata).
// A thin static registrar in the generated file calls that table. A reflected type that was never scanned
// is simply not registered. Defining RENSGA_STRIP_EDITOR_METADATA (runtime builds) leaves headers,
// tooltips, ranges and required components out of the generated tables.
// Reflected types must be default-constructible, hold at most kMaxFields fields, and not use multiple inheritance.
#pragma once
#include <new>

#include "rensga/core/guid.h"
#include "rensga/core/math.h"
#include "rensga/core/types.h"

namespace rensga {

class Component;
class Behaviour;
struct UnityEvent;  // unity_event.h (F3.05)

enum class FieldType : u8 { Scalar, I32, Bool, Vector3, Quaternion, Color32, Guid, UnityEvent };

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
  const char* header = nullptr;   // editor metadata: null when RENSGA_STRIP_EDITOR_METADATA
  const char* tooltip = nullptr;  // editor metadata
  bool hideInInspector = false;   // editor metadata: HIDE_IN_INSPECTOR (still serialized and cooked)
  u8 space = 0;                   // editor metadata: SPACE(n) pixels of gap above the field
};

// CONTEXT_MENU("Label") on a void() method: an entry in the component's header menu (editor metadata).
struct ContextMenuDesc {
  const char* label = nullptr;
  const char* method = nullptr;  // registered in the method registry (reflect_method.h)
};
constexpr u32 kMaxContextMenus = 8;

constexpr u32 kMaxFields = 32;

struct TypeInfo {
  const char* name = nullptr;
  u32 hash = 0;  // FNV-1a 32 of name
  u32 id = 0;    // index in the registry
  u32 size = 0;
  u32 align = 0;
  FieldDesc* fields = nullptr;
  u32 fieldCount = 0;
  const char* requireNames[4] = {};  // editor metadata: REQUIRE_COMPONENT type names
  u32 requireCount = 0;
  bool executeInEditMode = false;  // editor metadata: EXECUTE_IN_EDIT_MODE (behaviour wired in Editor 5)
  ContextMenuDesc contextMenus[kMaxContextMenus] = {};  // editor metadata: CONTEXT_MENU entries
  u32 contextMenuCount = 0;
  void (*construct)(void* where) = nullptr;
  void (*destruct)(void* obj) = nullptr;
  Component* (*asComponent)(void* obj) = nullptr;  // null if not a Component
  Behaviour* (*asBehaviour)(void* obj) = nullptr;  // null if not a Behaviour
  i32 executionOrder = 0;  // script execution order: lower runs first (EXECUTION_ORDER(n) in the class body)
  bool isBehaviour() const { return asBehaviour != nullptr; }
};

constexpr u32 kMaxTypes = 128;

class TypeRegistry {
 public:
  static TypeRegistry& instance();
  // find() also resolves renamed types: canonicalName() maps an old name (scene files, ConsoleTarget lists) to the current one.
  const TypeInfo* find(const char* name) const;
  // F4: the current name of a type that was renamed ("TilemapCollider" -> "TilemapCollider2D"); any other name is returned as is.
  // Use it wherever a type name read from a file is compared with another name (allowedComponents checks).
  static const char* canonicalName(const char* name);
  const TypeInfo* findByHash(u32 hash) const;
  const TypeInfo* byId(u32 id) const { return id < count_ ? &types_[id] : nullptr; }
  u32 count() const { return count_; }
  // Internal: used by the macros.
  TypeInfo* add(const char* name, u32 size, u32 align);

  // ---- script modules (Editor 4 hot reload) ----
  // A game module registers its types when it is loaded. mark() before the load and unregisterFrom(mark) before
  // the unload remove exactly those types again; engine types (registered first) never go away. Hooks run for
  // each type just before it is cleared, so registries keyed by TypeInfo* (methods, drawers) can forget it.
  // Worlds that still hold components of a removed type must be gone before that.
  typedef void (*UnregisterHook)(const TypeInfo*);
  static bool addUnregisterHook(UnregisterHook hook);  // false when the (small, fixed) hook table is full
  u32 mark() const { return count_; }
  void unregisterFrom(u32 mark);

 private:
  TypeInfo types_[kMaxTypes];
  u32 count_ = 0;
};

// ---- binary field blobs (pack FDAT) and defaults ---------------------------

// Total size of a type's blob (sum of fieldBlobSize).
u32 blobSize(const TypeInfo& t);
// Writes `obj`'s fields in declaration order. `out` must hold blobSize(t) bytes.
void writeBlob(const TypeInfo& t, const void* obj, u8* out);
// Reads fields into `obj`. F5: a blob SHORTER than blobSize(t) is accepted when it ends exactly on a field boundary; the
// missing trailing fields keep the values already in `obj` (their defaults). Component fields are only ever appended, so
// packs and scenes cooked before a field existed still load. A longer blob, or a cut inside a field, returns false (object
// untouched).
bool readBlob(const TypeInfo& t, void* obj, const u8* blob, u32 size);
// True if both objects have equal reflected field values.
bool fieldsEqual(const TypeInfo& t, const void* a, const void* b);

// ---- hot-reload state transfer (Editor 4 uses it) and field paths (prefab overrides, Editor 6 builds on them) ----

// Serializes the reflected fields of `obj` as a name-keyed state blob: [u32 count] then per field
// [u32 nameHash][u8 type][value bytes]. Returns the byte count, or 0 if `cap` is too small.
u32 stateBlobSize(const TypeInfo& t);
u32 saveState(const TypeInfo& t, const void* obj, u8* out, u32 cap);
// Restores by field name and type, so a recompiled type that added, removed or reordered fields keeps what still
// matches. Returns the number of fields restored, or -1 when the blob is malformed (object untouched).
int loadState(const TypeInfo& t, void* obj, const u8* data, u32 size);

// Field path syntax: "<field>" or "<field>.<part>" with part x|y|z|w (Vector3, Quaternion) or r|g|b|a (Color32).
// Scene-level paths prepend "<ComponentType>." (prefab overrides, Editor 6).
struct FieldPath {
  const FieldDesc* field = nullptr;
  int part = -1;  // -1 = whole field
};
bool parseFieldPath(const TypeInfo& t, const char* path, FieldPath& out);

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
template <> struct FieldTypeOf<UnityEvent> { static constexpr FieldType value = FieldType::UnityEvent; };

// Reads `static constexpr i32 kExecutionOrder` from a class (see EXECUTION_ORDER); 0 when absent.
template <class C, class = void>
struct ExecutionOrderOf { static constexpr i32 value = 0; };
template <class C>
struct ExecutionOrderOf<C, decltype((void)C::kExecutionOrder)> { static constexpr i32 value = C::kExecutionOrder; };

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
TypeInfo* beginType(const char* name) {
  TypeInfo* info = TypeRegistry::instance().add(name, sizeof(C), alignof(C));
  if (!info) return nullptr;
  info->fields = TypeBuilder<C>::fields;
  info->construct = [](void* where) { new (where) C(); };
  info->destruct = [](void* obj) { static_cast<C*>(obj)->~C(); };
  info->executionOrder = ExecutionOrderOf<C>::value;
  if constexpr (__is_base_of(Component, C)) {
    info->asComponent = [](void* obj) -> Component* { return static_cast<C*>(obj); };
  }
  if constexpr (__is_base_of(Behaviour, C)) {
    info->asBehaviour = [](void* obj) -> Behaviour* { return static_cast<C*>(obj); };
  }
  TypeBuilder<C>::info = info;
  return info;
}

// Adds a field to a type under construction. Returns the new descriptor (metadata is set by the generated table).
FieldDesc* addFieldDesc(TypeInfo* info, const char* name, FieldType type, u32 offset, u32 size);
void requireComponent(TypeInfo* info, const char* typeName);
void addContextMenu(TypeInfo* info, const char* label, const char* method);

template <class C, class T>
FieldDesc* addField(TypeInfo* info, const char* name, FieldType type, T C::*member) {
  return addFieldDesc(info, name, type, offsetOfMember<C, T>(member), sizeof(T));
}

}  // namespace reflect
}  // namespace rensga

#define REFLECT(Type)                                              \
 public:                                                           \
  using Self = Type;                                               \
  static const ::rensga::TypeInfo* staticTypeInfo() {              \
    return ::rensga::reflect::TypeBuilder<Type>::info;             \
  }

// Declarations only: the scanner reads these lines. They expand to nothing a compiler needs.
#define SERIALIZE_FIELD(type, name, def) type name = def;
#define RANGE(lo, hi) static_assert(true, "");
#define HEADER(text) static_assert(true, "");
#define TOOLTIP(text) static_assert(true, "");
#define REQUIRE_COMPONENT(Type) static_assert(true, "");
#define HIDE_IN_INSPECTOR() static_assert(true, "");
#define SPACE(...) static_assert(true, "");  // SPACE() = 8 px, SPACE(n) = n px
#define EXECUTE_IN_EDIT_MODE() static_assert(true, "");
#define CONTEXT_MENU(label) static_assert(true, "");
// Script execution order (default 0, lower runs first), read at compile time by the registry. Put it in the class
// body, e.g. `EXECUTION_ORDER(-100)` for an input script that must update before the rest.
#define EXECUTION_ORDER(n) static constexpr ::rensga::i32 kExecutionOrder = (n);
