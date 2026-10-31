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
// F6 field types (appended to FieldType, so older numbers keep their meaning):
//   String   SERIALIZE_FIELD(String32, label, String32("x"))   fixed 32 bytes in memory and in the blob, NUL terminated
//   Enum     SERIALIZE_ENUM(mode, 0, "Horizontal|Vertical|Grid")   an i32 whose labels are declared once; the inspector shows a
//            dropdown, JSON accepts the number or the label. Labels are editor metadata (stripped in runtime builds).
//   Vector2  SERIALIZE_FIELD(Vector2, size, Vector2(1, 1))
//   Array    SERIALIZE_ARRAY(Vector3, positions, 16)   FixedArray<T, N> with T = i32, Scalar, Guid or Vector3: a u32 count and N
//            slots. The blob is [u32 count][N * element], the unused slots written as zeros, so a cook is reproducible.
// APPENDED_FIELDS() before a SERIALIZE_FIELD marks where fields were appended after the type shipped: the cooker drops
// trailing appended fields that still hold their default from the blob, so packs of old scenes stay byte-identical.
//
// F1.05: the macros only declare. tools/reflect/reflect_scan.py reads the source at build time and writes
// <file>.reflect.gen.cpp, which #includes the file and registers the type table (names, offsets, editor metadata).
// A thin static registrar in the generated file calls that table. A reflected type that was never scanned
// is simply not registered. Defining RENSGA_STRIP_EDITOR_METADATA (runtime builds) leaves headers,
// tooltips, ranges and required components out of the generated tables.
// Reflected types must be default-constructible, hold at most kMaxFields fields, and not use multiple inheritance.
#pragma once
#include <new>
#include <stddef.h>

#include "rensga/core/guid.h"
#include "rensga/core/math.h"
#include "rensga/core/types.h"

namespace rensga {

class Component;
class Behaviour;
struct UnityEvent;  // unity_event.h (F3.05)

enum class FieldType : u8 { Scalar, I32, Bool, Vector3, Quaternion, Color32, Guid, UnityEvent, String, Enum, Vector2, Array };

// F6: a string field. 32 bytes including the NUL; longer text is cut. Copyable, comparable, usable as const char*.
struct String32 {
  static constexpr u32 kCapacity = 32;
  char s[kCapacity] = {};
  String32() {}
  String32(const char* text) { Set(text); }
  void Set(const char* text) {
    u32 i = 0;
    for (; text && text[i] && i < kCapacity - 1; ++i) s[i] = text[i];
    for (; i < kCapacity; ++i) s[i] = 0;
  }
  const char* c_str() const { return s; }
  operator const char*() const { return s; }
  bool empty() const { return s[0] == 0; }
  bool operator==(const String32& o) const {
    for (u32 i = 0; i < kCapacity; ++i) {
      if (s[i] != o.s[i]) return false;
      if (!s[i]) return true;
    }
    return true;
  }
  bool operator!=(const String32& o) const { return !(*this == o); }
};
static_assert(sizeof(String32) == 32, "String32 is 32 bytes");

// F6: fixed-capacity array field (SERIALIZE_ARRAY). T = i32, Scalar, Guid or Vector3. No heap.
template <class T, u32 N>
struct FixedArray {
  static constexpr u32 kCapacity = N;
  u32 count = 0;
  T items[N] = {};
  u32 size() const { return count; }
  bool Add(const T& v) {
    if (count >= N) return false;
    items[count++] = v;
    return true;
  }
  void Clear() { count = 0; }
  T& operator[](u32 i) { return items[i]; }
  const T& operator[](u32 i) const { return items[i]; }
};

// Byte size of a field value in the pack blob (and in memory for all but Bool padding rules). Array needs the descriptor (below);
// this overload returns 0 for it.
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
  // F6.
  const char* enumLabels = nullptr;  // Enum: "A|B|C" (index = value); editor metadata, null when stripped
  FieldType elemType = FieldType::Scalar;  // Array: I32, Scalar, Guid or Vector3
  u16 arrayCap = 0;                        // Array: N
  bool isEnum() const { return type == FieldType::Enum; }
  bool isArray() const { return type == FieldType::Array; }
};

// Byte size of this field's value in the pack blob (handles Array). The in-memory bytes of every type are identical, except
// that the unused slots of an Array and the bytes after a String's NUL are zeroed when written.
u32 fieldBlobSize(const FieldDesc& f);
// Number of labels of an Enum field and the n-th label copied into `out` (NUL terminated, "" if out of range).
u32 enumLabelCount(const FieldDesc& f);
bool enumLabel(const FieldDesc& f, u32 index, char* out, u32 cap);
// Index of `label` (exact, case sensitive) or -1.
i32 enumIndexOf(const FieldDesc& f, const char* label);

// CONTEXT_MENU("Label") on a void() method: an entry in the component's header menu (editor metadata).
struct ContextMenuDesc {
  const char* label = nullptr;
  const char* method = nullptr;  // registered in the method registry (reflect_method.h)
};
constexpr u32 kMaxContextMenus = 8;

constexpr u32 kMaxFields = 32;
constexpr u32 kNoTrim = 0xFFFFFFFFu;

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
  // F6: fields from this index on were appended after the type shipped (APPENDED_FIELDS()). The cooker writes a blob without the
  // trailing appended fields that still hold their default. kNoTrim = write every field.
  u32 trimFloor = kNoTrim;
  bool isBehaviour() const { return asBehaviour != nullptr; }
};

constexpr u32 kMaxTypes = 192;  // F6: was 128; engine types ~75 after F6, the rest is for scripts

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
// F6: size of the blob without trailing appended fields that equal the defaults of a default-constructed object (see
// TypeInfo::trimFloor). Equal to blobSize(t) for a type with no APPENDED_FIELDS(). `defaults` must be a default-constructed object.
u32 trimmedBlobSize(const TypeInfo& t, const void* obj, const void* defaults);
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

// Field path syntax: "<field>" or "<field>.<part>" with part x|y|z|w (Vector2, Vector3, Quaternion) or r|g|b|a (Color32).
// F6: an Array field takes "<field>.<index>" and "<field>.<index>.<part>" (Vector3 elements).
// Scene-level paths prepend "<ComponentType>." (prefab overrides, Editor 6).
struct FieldPath {
  const FieldDesc* field = nullptr;
  int part = -1;   // -1 = whole field (or whole element)
  int index = -1;  // F6: array element, -1 = the whole array (or not an array)
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
template <> struct FieldTypeOf<String32> { static constexpr FieldType value = FieldType::String; };
template <> struct FieldTypeOf<Vector2> { static constexpr FieldType value = FieldType::Vector2; };

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

// SERIALIZE_ARRAY: the element type and capacity come from the member's FixedArray<E, N> type.
template <class C, class E, u32 N>
FieldDesc* addArrayField(TypeInfo* info, const char* name, FixedArray<E, N> C::*member) {
  static_assert(sizeof(E) == 4 || sizeof(E) == 12 || sizeof(E) == 16, "array elements: i32, Scalar, Guid or Vector3");
  static_assert(FieldTypeOf<E>::value == FieldType::I32 || FieldTypeOf<E>::value == FieldType::Scalar ||
                    FieldTypeOf<E>::value == FieldType::Guid || FieldTypeOf<E>::value == FieldType::Vector3,
                "array elements: i32, Scalar, Guid or Vector3");
  using Arr = FixedArray<E, N>;
  static_assert(offsetof(Arr, items) == 4 && sizeof(Arr) == 4 + N * sizeof(E), "array items follow the count");
  FieldDesc* f = addFieldDesc(info, name, FieldType::Array, offsetOfMember<C, Arr>(member), sizeof(Arr));
  if (f) {
    f->elemType = FieldTypeOf<E>::value;
    f->arrayCap = static_cast<u16>(N);
  }
  return f;
}
// APPENDED_FIELDS(): the fields declared so far shipped before; later ones are trimmed from the cooked blob when default.
inline void setTrimFloor(TypeInfo* info) {
  if (info) info->trimFloor = info->fieldCount;
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
#define SERIALIZE_ENUM(name, def, labels) ::rensga::i32 name = (def);
#define SERIALIZE_ARRAY(type, name, capacity) ::rensga::FixedArray<type, capacity> name;
#define APPENDED_FIELDS() static_assert(true, "");
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
