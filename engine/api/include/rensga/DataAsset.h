// F6.07 (Editor 13): data assets, the ScriptableObject equivalent. A data asset is a plain reflected struct that holds tuning
// values. It is not a component and lives in no scene. The designer creates one in the Project window (Create > Data Asset >
// <Type>), edits it in the Inspector (undo works), and points a Guid field of a component at it. The cooker writes every
// *.asset.json into the pack's DATA section; at run time DataAssets::Get<T> turns the Guid into a read-only pointer.
//
//   // Assets/Scripts/EnemyStats.h
//   struct EnemyStats : DataAsset {
//     REFLECT(EnemyStats)
//     SERIALIZE_FIELD(i32, health, 30)
//     RANGE(0, 20)
//     SERIALIZE_FIELD(Scalar, speed, 3.0f)
//     SERIALIZE_FIELD(String32, label, "Grunt")
//   };
//   // Assets/Scripts/Enemy.cpp
//   struct Enemy : Behaviour {
//     REFLECT(Enemy)
//     SERIALIZE_FIELD(Guid, stats, Guid())        // drag an EnemyStats asset onto it in the Inspector
//     const EnemyStats* s = nullptr;
//     void Start() override { s = DataAssets::Get<EnemyStats>(stats); }   // null when unset or of another type
//   };
//
// Rules: fields use the reflected types (Scalar, i32, bool, Vector2/3, Color32, String32, Guid, SERIALIZE_ENUM, SERIALIZE_ARRAY),
// at most kDataAssetMaxBytes per instance. The returned pointer is valid for the whole run (and is refreshed when a new pack is
// bound, for example when the editor plays again). No heap: instances live in a fixed table. Treat the data as read-only; to
// change values at run time copy them into the component.
#pragma once
#include "rensga/core/guid.h"
#include "rensga/core/reflect.h"

namespace rensga {

// Marker base. A struct derives from it to say "I am a data asset" (it adds no data and no virtuals).
struct DataAsset {};

constexpr u32 kDataAssetMaxBytes = 512;  // largest instance and largest cooked blob
constexpr u32 kDataAssetMaxLive = 24;    // data assets that can be in use at the same time

namespace DataAssets {
// The instance for `guid` if it is a data asset of type `type`; null for a null Guid, an unknown Guid, another type or a full table
// (each case logs once). Built from the pack on first use, so call it from Awake or Start, not in a hot loop (it is cheap anyway).
const void* Find(const Guid& guid, const TypeInfo* type);
template <class T>
const T* Get(const Guid& guid) {
  return static_cast<const T*>(Find(guid, T::staticTypeInfo()));
}
u32 count();      // data assets in the bound pack
u32 liveCount();  // instances built so far
}  // namespace DataAssets

}  // namespace rensga
