// F6.03 (B1): property clip sampler. See rensga/core/prop_clip.h. Integer Q16 time, float math with + - * / only (RULES 1.9).
#include "rensga/core/prop_clip.h"

#include <string.h>

#include "rensga/core/animator.h"

namespace rensga {
namespace PropClips {

namespace {
constexpr u32 kCacheSize = 64;
struct CacheEntry {
  u32 typeHash = 0, fieldHash = 0;
  const TypeInfo* info = nullptr;
  const FieldDesc* desc = nullptr;
  u32 special = 0;
  bool used = false;
};
CacheEntry g_cache[kCacheSize];
bool g_hooked = false;

void clearCache(const TypeInfo*) {
  for (CacheEntry& e : g_cache) e.used = false;
}

u32 hashBits(u32 h, u32 v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (i * 8)) & 255u;
    h *= 16777619u;
  }
  return h;
}
u32 floatBits(Scalar f) {
  u32 b;
  memcpy(&b, &f, 4);
  return b;
}

bool lookup(u32 typeHash, u32 fieldHash, CacheEntry& out) {
  if (!g_hooked) {
    g_hooked = true;
    TypeRegistry::addUnregisterHook(&clearCache);  // a script hot reload drops TypeInfo pointers
  }
  const u32 start = (typeHash * 31u + fieldHash) % kCacheSize;
  for (u32 n = 0; n < kCacheSize; ++n) {
    CacheEntry& e = g_cache[(start + n) % kCacheSize];
    if (e.used && e.typeHash == typeHash && e.fieldHash == fieldHash) { out = e; return true; }
    if (!e.used) {
      // Miss: resolve, remember.
      e.typeHash = typeHash;
      e.fieldHash = fieldHash;
      e.info = nullptr;
      e.desc = nullptr;
      e.special = 0;
      if (typeHash == kTransformTypeHash) {
        if (fieldHash == fnv1a32("localPosition") || fieldHash == fnv1a32("localRotation") || fieldHash == fnv1a32("localScale")) e.special = 1;
      } else if (typeHash == kGameObjectTypeHash) {
        if (fieldHash == fnv1a32("active")) e.special = 2;
      } else if (const TypeInfo* ti = TypeRegistry::instance().findByHash(typeHash)) {
        for (u32 f = 0; f < ti->fieldCount; ++f)
          if (fnv1a32(ti->fields[f].name) == fieldHash) { e.info = ti; e.desc = &ti->fields[f]; break; }
      }
      e.used = e.special != 0 || e.desc != nullptr;
      if (!e.used) return false;
      out = e;
      return true;
    }
  }
  // Cache full: resolve without caching.
  out = CacheEntry();
  if (typeHash == kTransformTypeHash) out.special = fieldHash == fnv1a32("localPosition") || fieldHash == fnv1a32("localRotation") || fieldHash == fnv1a32("localScale") ? 1 : 0;
  else if (typeHash == kGameObjectTypeHash) out.special = fieldHash == fnv1a32("active") ? 2 : 0;
  else if (const TypeInfo* ti = TypeRegistry::instance().findByHash(typeHash))
    for (u32 f = 0; f < ti->fieldCount; ++f)
      if (fnv1a32(ti->fields[f].name) == fieldHash) { out.info = ti; out.desc = &ti->fields[f]; break; }
  return out.special != 0 || out.desc != nullptr;
}

i32 roundToI32(Scalar v) { return static_cast<i32>(v < 0 ? v - 0.5f : v + 0.5f); }
Scalar blend(Scalar cur, Scalar v, Scalar w) { return w >= 1.0f ? v : cur + (v - cur) * w; }
u8 toByte(Scalar v) { return static_cast<u8>(v <= 0 ? 0 : (v >= 255.0f ? 255 : static_cast<i32>(v + 0.5f))); }

void normalizeQuat(Quaternion* q) {
  const Scalar n = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
  if (n < 1e-12f) { *q = Quaternion::identity(); return; }
  *q = q->normalized();
}
}  // namespace

bool resolveField(u32 typeHash, u32 fieldHash, const TypeInfo** info, const FieldDesc** desc, u32* special) {
  CacheEntry e;
  if (!lookup(typeHash, fieldHash, e)) return false;
  if (info) *info = e.info;
  if (desc) *desc = e.desc;
  if (special) *special = e.special;
  return true;
}

GameObject* findByPath(GameObject* root, const char* path) {
  if (!root) return nullptr;
  if (!path || !*path) return root;
  GameObject* cur = root;
  const char* p = path;
  while (*p) {
    const char* end = p;
    while (*end && *end != '/') ++end;
    const size_t len = static_cast<size_t>(end - p);
    GameObject* found = nullptr;
    for (Transform* c = cur->transform.firstChild_; c; c = c->nextSibling_) {
      GameObject* g = c->gameObject;
      if (g && strlen(g->name) == len && strncmp(g->name, p, len) == 0) { found = g; break; }
    }
    if (!found) return nullptr;
    cur = found;
    p = *end ? end + 1 : end;
  }
  return cur;
}

Scalar sampleCurve(const PropClipSource& src, const PackPropCurve& c, u64 tq) {
  const u32 n = c.keyCount;
  if (n == 0) return 0;
  const PackPropKey* k = src.keys(c);
  const bool isGuid = c.valueType == static_cast<u8>(FieldType::Guid);
  const bool step = c.interp == kPropInterpStep || isGuid || c.valueType == static_cast<u8>(FieldType::I32) ||
                    c.valueType == static_cast<u8>(FieldType::Bool) || c.valueType == static_cast<u8>(FieldType::Enum);
  if (n == 1 || tq <= (static_cast<u64>(k[0].tick) << 16)) return isGuid ? 0.0f : k[0].value;
  if (tq >= (static_cast<u64>(k[n - 1].tick) << 16)) return isGuid ? static_cast<Scalar>(n - 1) : k[n - 1].value;
  u32 lo = 0, hi = n - 1;  // k[lo] <= tq < k[hi]
  while (hi - lo > 1) {
    const u32 mid = (lo + hi) / 2;
    if ((static_cast<u64>(k[mid].tick) << 16) <= tq) lo = mid; else hi = mid;
  }
  if (isGuid) return static_cast<Scalar>(lo);
  if (step) return k[lo].value;
  const u32 dt = k[hi].tick - k[lo].tick;
  if (dt == 0) return k[hi].value;
  const Scalar s = static_cast<Scalar>(static_cast<u32>(tq - (static_cast<u64>(k[lo].tick) << 16))) / static_cast<Scalar>(static_cast<u64>(dt) << 16);
  const Scalar p0 = k[lo].value, p1 = k[hi].value;
  if (c.interp == kPropInterpHermite) {
    const Scalar s2 = s * s, s3 = s2 * s;
    const Scalar d = static_cast<Scalar>(dt);
    return (2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * d * k[lo].outTangent + (-2 * s3 + 3 * s2) * p1 + (s3 - s2) * d * k[hi].inTangent;
  }
  return p0 + (p1 - p0) * s;
}

u32 apply(World& world, GameObject* root, const PropClipSource& src, u64 tq, Scalar weight, u32* hash) {
  if (!src.valid() || !root || weight <= 0) return 0;
  const PackPropCurve* curves = src.curves();
  u32 written = 0;
  Quaternion* quats[8];
  u32 quatCount = 0;
  for (u32 ci = 0; ci < src.clip->curveCount; ++ci) {
    const PackPropCurve& c = curves[ci];
    GameObject* go = findByPath(root, src.str(c.pathStr));
    if (!go) continue;
    CacheEntry e;
    if (!lookup(c.componentType, c.fieldHash, e)) continue;
    const Scalar v = sampleCurve(src, c, tq);
    const bool discrete = c.valueType == static_cast<u8>(FieldType::I32) || c.valueType == static_cast<u8>(FieldType::Bool) ||
                          c.valueType == static_cast<u8>(FieldType::Enum) || c.valueType == static_cast<u8>(FieldType::Guid);
    if (discrete && weight < 0.5f) continue;
    if (e.special == 2) {  // GameObject.active
      const bool on = v >= 0.5f;
      if (go->activeSelf() != on) world.setObjectActive(go, on);
      if (hash) *hash = hashBits(*hash, on ? 1u : 0u);
      ++written;
      continue;
    }
    Scalar* sp = nullptr;
    u8* base = nullptr;
    FieldType ft = FieldType::Scalar;
    if (e.special == 1) {
      Transform& t = go->transform;
      if (c.fieldHash == fnv1a32("localPosition")) { sp = &t.lpos_.x; ft = FieldType::Vector3; }
      else if (c.fieldHash == fnv1a32("localScale")) { sp = &t.lscale_.x; ft = FieldType::Vector3; }
      else { sp = &t.lrot_.x; ft = FieldType::Quaternion; }
    } else {
      Component* comp = world.getComponent(go, *e.info);
      if (!comp) continue;
      base = static_cast<u8*>(static_cast<void*>(comp)) + e.desc->offset;
      ft = e.desc->type;
      sp = reinterpret_cast<Scalar*>(base);
    }
    switch (ft) {
      case FieldType::Scalar:
        if (c.part >= 0) continue;
        *sp = blend(*sp, v, weight);
        if (hash) *hash = hashBits(*hash, floatBits(*sp));
        break;
      case FieldType::Vector2:
      case FieldType::Vector3:
      case FieldType::Quaternion: {
        const int parts = ft == FieldType::Vector2 ? 2 : (ft == FieldType::Vector3 ? 3 : 4);
        if (c.part < 0 || c.part >= parts) continue;
        Scalar* d = sp + c.part;
        *d = blend(*d, v, weight);
        if (hash) *hash = hashBits(*hash, floatBits(*d));
        if (ft == FieldType::Quaternion && (c.part == 3 || c.part == 2) && quatCount < 8) {
          Quaternion* q = reinterpret_cast<Quaternion*>(sp);
          bool dup = false;
          for (u32 i = 0; i < quatCount; ++i) dup = dup || quats[i] == q;
          if (!dup) quats[quatCount++] = q;
        }
        break;
      }
      case FieldType::Color32: {
        if (c.part < 0 || c.part > 3) continue;
        u8* d = base + c.part;
        *d = toByte(blend(static_cast<Scalar>(*d), v, weight));
        if (hash) *hash = hashBits(*hash, *d);
        break;
      }
      case FieldType::I32:
      case FieldType::Enum:
        *reinterpret_cast<i32*>(base) = roundToI32(v);
        if (hash) *hash = hashBits(*hash, static_cast<u32>(*reinterpret_cast<i32*>(base)));
        break;
      case FieldType::Bool:
        *reinterpret_cast<bool*>(base) = v >= 0.5f;
        if (hash) *hash = hashBits(*hash, v >= 0.5f ? 1u : 0u);
        break;
      case FieldType::Guid: {
        const Guid* table = src.guids(c);
        const u32 idx = static_cast<u32>(v);
        if (!table || idx >= c.keyCount) continue;
        *reinterpret_cast<Guid*>(base) = table[idx];
        if (hash) *hash = hashBits(*hash, table[idx].bytes[0] | (table[idx].bytes[1] << 8) | (table[idx].bytes[15] << 16));
        break;
      }
      default:
        continue;
    }
    ++written;
  }
  for (u32 i = 0; i < quatCount; ++i) normalizeQuat(quats[i]);
  return written;
}

u32 fireEvents(GameObject* target, const PropClipSource& src, i64 fromExcl, i64 toIncl) {
  if (!src.valid() || !target || src.clip->eventCount == 0) return 0;
  const PackPropEvent* ev = src.events();
  u32 fired = 0;
  for (u32 i = 0; i < src.clip->eventCount; ++i) {
    const i64 q = static_cast<i64>(ev[i].tick) << 16;
    if (q <= fromExcl || q > toIncl) continue;
    AnimationEvent e;
    e.functionName = src.str(ev[i].functionStr);
    e.intParameter = ev[i].intParameter;
    e.floatParameter = ev[i].floatParameter;
    e.stringParameter = src.str(ev[i].stringStr);
    e.time = static_cast<Scalar>(ev[i].tick) / static_cast<Scalar>(src.clip->fps);
    AnimationEvents::dispatch(target, e);
    ++fired;
  }
  return fired;
}

}  // namespace PropClips
}  // namespace rensga
