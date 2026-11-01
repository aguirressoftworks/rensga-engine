// F3.05: UnityEvent, an event configured in the inspector and stored in scene data (RULES 1.8).
//
//   struct Door : Behaviour {
//     REFLECT(Door)
//     SERIALIZE_FIELD(UnityEvent, OnOpened, {})   // the inspector lists listeners; stage 2 adds the drawer
//   };
//   OnOpened.Invoke();
//
// A UnityEvent is plain data (fixed size, no pointers): up to kMaxUnityEventListeners listeners, each
// {target object GUID, component type, method, argument}. Targets are found by GUID when the event fires,
// so the same bytes work in the scene JSON, the cooked pack and every platform.
//
// JSON shape (reflect_json), the field value is an array, empty array = no listeners:
//   [ {"target": "<object guid>", "type": "Light", "method": "SetIntensity", "arg": 2.5}, ... ]
//   "arg" is a number (Scalar/i32), true/false (bool) or a GUID string (Object); absent for void() methods.
// Cooked form: the struct bytes as they are in memory (sizeof(UnityEvent) = 180); a type that has no UnityEvent
// field cooks exactly as before, so old packs load unchanged.
#pragma once
#include "rensga/core/guid.h"
#include "rensga/core/reflect_method.h"

namespace rensga {

class World;
class Component;

constexpr u32 kMaxUnityEventListeners = 4;

struct UnityEventListener {
  Guid target;      // object GUID; null = empty slot
  u32 typeHash = 0;    // fnv1a32 of the component type name
  u32 methodHash = 0;  // fnv1a32 of the method name
  u8 argKind = 0;      // MethodArgKind
  u8 dynamic = 0;      // F6 (D1): 1 = the method gets the value the event is raised with (Invoke(world, arg)), not a stored argument
  u8 pad_[2] = {};
  u8 arg[16] = {};  // Scalar/i32: first 4 bytes, bool: first byte, Guid: 16 bytes

  MethodArg decode() const;
  void setScalar(Scalar v);
  void setI32(i32 v);
  void setBool(bool v);
  void setObject(const Guid& g);
};
static_assert(sizeof(UnityEventListener) == 44, "UnityEventListener is 44 bytes (pack format)");

struct UnityEvent {
  u32 count = 0;
  UnityEventListener listeners[kMaxUnityEventListeners];

  // Fires every listener in order on the active World / on `world`. Missing targets, destroyed or disabled-away
  // objects and unknown methods are skipped. Returns the number of methods called.
  u32 Invoke() const;
  u32 Invoke(World& world) const;
  // F6 (D1): dynamic-argument invoke (Unity's UnityEvent<T>): listeners marked `dynamic` whose method takes an argument get `arg`
  // (converted to the method's kind: Scalar <-> i32, bool <-> number); every other listener behaves as in Invoke(world).
  u32 InvokeWith(World& world, MethodArgKind kind, const MethodArg& arg) const;
  u32 InvokeWith(MethodArgKind kind, const MethodArg& arg) const;

  // Adds a listener; null when full. `arg` is read only for one-argument methods.
  UnityEventListener* AddListener(const Guid& target, const char* componentType, const char* method);
  void RemoveAllListeners() { count = 0; }
  // Logs a warning for every listener that cannot be resolved in `world` (called after a pack loads).
  u32 validate(World& world, const char* ownerName) const;
};
static_assert(sizeof(UnityEvent) == 180, "UnityEvent is 180 bytes (pack format)");

}  // namespace rensga
