// F6.03 / F6.10 (B1): property clips. A clip keyframes any reflected field of any component by name (plus the pseudo components
// "Transform" and "GameObject", see below). Time is an integer count of Q16 clip ticks (65536 = one tick of the clip's own fps), so
// Linux, the PSP simulator and the console agree bit for bit (RULES 1.9). Only + - * / and no libm run on the sampling path.
//
// The same code plays a cooked PCLP record (runtime) and a clip baked in memory by the editor (Animation window preview): both are a
// PackPropClip plus its BLOB and STRS bytes, wrapped in PropClipSource.
//
// Pseudo components (not reflected types):
//   Transform   localPosition (Vector3), localRotation (Quaternion x y z w), localScale (Vector3)
//   GameObject  active (Bool)
// Curve layout, interpolation and the Guid table are documented in docs/rensga-engine/animation.md and pack-format.md.
#pragma once
#include "rensga/core/pack_format.h"
#include "rensga/core/world.h"

namespace rensga {

// PackPropCurve::interp values. 0 and 1 come from the contract (pack_format.h); 2 is added by B1. Bool, I32, Enum and Guid curves
// are always read as step.
constexpr u8 kPropInterpHermite = 2;  // cubic Hermite from the keys' in / out tangents (per tick): Unity's "free smooth" curve
constexpr u32 kTransformTypeHash = fnv1a32("Transform");
constexpr u32 kGameObjectTypeHash = fnv1a32("GameObject");

// A Guid-valued curve (FieldType::Guid) keeps its key values in a table: PackPropCurve::reserved[0] is the BLOB offset of a
// Guid[keyCount] (key k holds guid k) and PackPropKey::value is unused. Zero offset = no table.
inline u32 propCurveGuidTable(const PackPropCurve& c) { return c.reserved[0]; }

struct PropClipSource {
  const PackPropClip* clip = nullptr;
  const u8* blob = nullptr;
  u32 blobSize = 0;
  const char* strs = nullptr;
  u32 strsSize = 0;

  bool valid() const { return clip && blob && strs && clip->fps > 0; }
  const char* str(u32 off) const { return strs && off < strsSize ? strs + off : ""; }
  const PackPropCurve* curves() const { return reinterpret_cast<const PackPropCurve*>(blob + clip->curvesOffset); }
  const PackPropEvent* events() const { return reinterpret_cast<const PackPropEvent*>(blob + clip->eventsOffset); }
  const PackPropKey* keys(const PackPropCurve& c) const { return reinterpret_cast<const PackPropKey*>(blob + c.keysOffset); }
  const Guid* guids(const PackPropCurve& c) const {
    const u32 off = propCurveGuidTable(c);
    return off && static_cast<u64>(off) + static_cast<u64>(c.keyCount) * sizeof(Guid) <= blobSize ? reinterpret_cast<const Guid*>(blob + off) : nullptr;
  }
};

namespace PropClips {
// Clip length in Q16 ticks.
inline u64 lengthQ16(const PropClipSource& s) { return static_cast<u64>(s.clip->lengthTicks) << 16; }
// Q16 clip ticks one fixed step advances at speed 1 (tickHz = fixed steps per second): fps * 65536 / tickHz.
inline u64 stepQ16(const PropClipSource& s, u32 tickHz) { return tickHz ? (static_cast<u64>(s.clip->fps) << 16) / tickHz : 0; }

// The value of one curve at `timeQ16`, clamped to the first and last key. Guid curves return the key index (see above).
Scalar sampleCurve(const PropClipSource& src, const PackPropCurve& curve, u64 timeQ16);

// Writes every curve of the clip at `timeQ16` into the subtree under `root` (curve paths are child names joined by '/'). `weight`
// 1 replaces the value, 0 < weight < 1 blends from the current value (Bool, I32, Enum and Guid switch at 0.5). Quaternions are
// renormalised after a write. `hash` (optional) is folded with the bits of every value written. Returns the curves written.
u32 apply(World& world, GameObject* root, const PropClipSource& src, u64 timeQ16, Scalar weight, u32* hash = nullptr);

// Dispatches the events with fromExclQ16 < tick * 65536 <= toInclQ16 through AnimationEvents::dispatch on `target`.
u32 fireEvents(GameObject* target, const PropClipSource& src, i64 fromExclQ16, i64 toInclQ16);

// The object `path` ("A/B", "" = root) below `root`, matching child names; null when a name is missing.
GameObject* findByPath(GameObject* root, const char* path);

// Field lookup the curves use (cached, no heap). `type` / `field` are the hashes stored in the curve. Sets *special to 1 for a
// Transform field, 2 for GameObject.active, 0 for a reflected field (then *info and *desc are set).
bool resolveField(u32 typeHash, u32 fieldHash, const TypeInfo** info, const FieldDesc** desc, u32* special);
}  // namespace PropClips

}  // namespace rensga
