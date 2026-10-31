#include "rensga/core/reflect.h"

#include <string.h>


#include "rensga/core/log.h"
#include "rensga/core/unity_event.h"

namespace rensga {

u32 fieldBlobSize(FieldType t) {
  switch (t) {
    case FieldType::Scalar: return sizeof(Scalar);
    case FieldType::I32: return 4;
    case FieldType::Bool: return 1;
    case FieldType::Vector3: return 3 * sizeof(Scalar);
    case FieldType::Quaternion: return 4 * sizeof(Scalar);
    case FieldType::Color32: return 4;
    case FieldType::Guid: return 16;
    case FieldType::UnityEvent: return sizeof(UnityEvent);
    case FieldType::String: return String32::kCapacity;
    case FieldType::Enum: return 4;
    case FieldType::Vector2: return 2 * sizeof(Scalar);
    case FieldType::Array: return 0;  // needs the descriptor
  }
  return 0;
}

u32 fieldBlobSize(const FieldDesc& f) {
  if (f.type == FieldType::Array) return 4 + static_cast<u32>(f.arrayCap) * fieldBlobSize(f.elemType);
  return fieldBlobSize(f.type);
}

u32 enumLabelCount(const FieldDesc& f) {
  if (!f.enumLabels || !*f.enumLabels) return 0;
  u32 n = 1;
  for (const char* p = f.enumLabels; *p; ++p)
    if (*p == '|') ++n;
  return n;
}

bool enumLabel(const FieldDesc& f, u32 index, char* out, u32 cap) {
  if (cap) out[0] = 0;
  if (!f.enumLabels || !cap) return false;
  const char* p = f.enumLabels;
  for (u32 i = 0; i < index; ++i) {
    while (*p && *p != '|') ++p;
    if (!*p) return false;
    ++p;
  }
  u32 n = 0;
  while (p[n] && p[n] != '|') ++n;
  if (n >= cap) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
  return true;
}

i32 enumIndexOf(const FieldDesc& f, const char* label) {
  char buf[64];
  const u32 n = enumLabelCount(f);
  for (u32 i = 0; i < n; ++i)
    if (enumLabel(f, i, buf, sizeof(buf)) && strcmp(buf, label) == 0) return static_cast<i32>(i);
  return -1;
}

const char* fieldTypeName(FieldType t) {
  switch (t) {
    case FieldType::Scalar: return "Scalar";
    case FieldType::I32: return "I32";
    case FieldType::Bool: return "Bool";
    case FieldType::Vector3: return "Vector3";
    case FieldType::Quaternion: return "Quaternion";
    case FieldType::Color32: return "Color32";
    case FieldType::Guid: return "Guid";
    case FieldType::UnityEvent: return "UnityEvent";
    case FieldType::String: return "String";
    case FieldType::Enum: return "Enum";
    case FieldType::Vector2: return "Vector2";
    case FieldType::Array: return "Array";
  }
  return "?";
}

TypeRegistry& TypeRegistry::instance() {
  static TypeRegistry r;
  return r;
}

const char* TypeRegistry::canonicalName(const char* name) {
  static const struct { const char* old; const char* now; } kRenamed[] = {{"TilemapCollider", "TilemapCollider2D"}};
  for (const auto& r : kRenamed)
    if (strcmp(name, r.old) == 0) return r.now;
  return name;
}

const TypeInfo* TypeRegistry::find(const char* name) const {
  name = canonicalName(name);
  for (u32 i = 0; i < count_; ++i)
    if (strcmp(types_[i].name, name) == 0) return &types_[i];
  return nullptr;
}

const TypeInfo* TypeRegistry::findByHash(u32 hash) const {
  for (u32 i = 0; i < count_; ++i)
    if (types_[i].hash == hash) return &types_[i];
  return nullptr;
}

TypeInfo* TypeRegistry::add(const char* name, u32 size, u32 align) {
  if (find(name)) {
    RLOG_ERROR("reflect", "type '%s' registered twice", name);
    return nullptr;
  }
  if (count_ >= kMaxTypes) {
    RLOG_ERROR("reflect", "type registry full (%u)", kMaxTypes);
    return nullptr;
  }
  TypeInfo& t = types_[count_];
  t.name = name;
  t.hash = fnv1a32(name);
  t.id = count_;
  t.size = size;
  t.align = align;
  if (const TypeInfo* clash = findByHash(t.hash)) {
    RLOG_ERROR("reflect", "type hash clash between '%s' and '%s'", name, clash->name);
    return nullptr;
  }
  ++count_;
  return &t;
}

namespace {
constexpr u32 kMaxUnregisterHooks = 8;
TypeRegistry::UnregisterHook g_unregisterHooks[kMaxUnregisterHooks];
u32 g_unregisterHookCount = 0;
}  // namespace

bool TypeRegistry::addUnregisterHook(UnregisterHook hook) {
  if (!hook) return false;
  for (u32 i = 0; i < g_unregisterHookCount; ++i)
    if (g_unregisterHooks[i] == hook) return true;
  if (g_unregisterHookCount >= kMaxUnregisterHooks) return false;
  g_unregisterHooks[g_unregisterHookCount++] = hook;
  return true;
}

void TypeRegistry::unregisterFrom(u32 markCount) {
  if (markCount >= count_) return;
  for (u32 i = count_; i > markCount; --i) {
    const TypeInfo* t = &types_[i - 1];
    for (u32 h = 0; h < g_unregisterHookCount; ++h) g_unregisterHooks[h](t);
  }
  for (u32 i = markCount; i < count_; ++i) types_[i] = TypeInfo();
  count_ = markCount;
}

namespace reflect {

FieldDesc* addFieldDesc(TypeInfo* info, const char* name, FieldType type, u32 offset, u32 size) {
  if (!info) return nullptr;
  if (info->fieldCount >= kMaxFields) {
    RLOG_ERROR("reflect", "type '%s' has more than %u fields", info->name, kMaxFields);
    return nullptr;
  }
  FieldDesc& f = info->fields[info->fieldCount++];
  f = FieldDesc();
  f.name = name;
  f.type = type;
  f.offset = offset;
  f.size = size;
  return &f;
}

void requireComponent(TypeInfo* info, const char* typeName) {
  if (!info || info->requireCount >= 4) return;
  info->requireNames[info->requireCount++] = typeName;
}

void addContextMenu(TypeInfo* info, const char* label, const char* method) {
  if (!info || info->contextMenuCount >= kMaxContextMenus) return;
  info->contextMenus[info->contextMenuCount++] = ContextMenuDesc{label, method};
}

}  // namespace reflect

namespace {

// One field value: memory -> blob. Strings and unused array slots are written canonically (zero after the NUL / past the count).
void writeField(const FieldDesc& f, const u8* src, u8* out) {
  switch (f.type) {
    case FieldType::Color32: {
      const Color32* c = reinterpret_cast<const Color32*>(src);
      out[0] = c->r; out[1] = c->g; out[2] = c->b; out[3] = c->a;
      break;
    }
    case FieldType::String: {
      u32 i = 0;
      for (; i < String32::kCapacity && src[i]; ++i) out[i] = src[i];
      for (; i < String32::kCapacity; ++i) out[i] = 0;
      break;
    }
    case FieldType::Array: {
      const u32 es = fieldBlobSize(f.elemType);
      u32 count;
      memcpy(&count, src, 4);
      if (count > f.arrayCap) count = f.arrayCap;
      memcpy(out, &count, 4);
      memcpy(out + 4, src + 4, count * es);
      memset(out + 4 + count * es, 0, (f.arrayCap - count) * es);
      break;
    }
    default: memcpy(out, src, fieldBlobSize(f.type)); break;
  }
}

void readField(const FieldDesc& f, u8* dst, const u8* blob) {
  switch (f.type) {
    case FieldType::Color32: *reinterpret_cast<Color32*>(dst) = Color32(blob[0], blob[1], blob[2], blob[3]); break;
    case FieldType::String:
      memcpy(dst, blob, String32::kCapacity);
      dst[String32::kCapacity - 1] = 0;
      break;
    case FieldType::Array: {
      const u32 es = fieldBlobSize(f.elemType);
      u32 count;
      memcpy(&count, blob, 4);
      if (count > f.arrayCap) count = f.arrayCap;
      memcpy(dst, &count, 4);
      memcpy(dst + 4, blob + 4, f.arrayCap * es);
      break;
    }
    default: memcpy(dst, blob, fieldBlobSize(f.type)); break;
  }
}

bool fieldEqual(const FieldDesc& f, const u8* a, const u8* b) {
  switch (f.type) {
    case FieldType::String: return strncmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b), String32::kCapacity) == 0;
    case FieldType::Array: {
      u32 ca, cb;
      memcpy(&ca, a, 4);
      memcpy(&cb, b, 4);
      if (ca != cb) return false;
      if (ca > f.arrayCap) ca = f.arrayCap;
      return memcmp(a + 4, b + 4, ca * fieldBlobSize(f.elemType)) == 0;
    }
    default:  // value bytes of the in-memory representation (all other field types are plain data)
      return memcmp(a, b, fieldBlobSize(f.type)) == 0;
  }
}

}  // namespace

u32 blobSize(const TypeInfo& t) {
  u32 n = 0;
  for (u32 i = 0; i < t.fieldCount; ++i) n += fieldBlobSize(t.fields[i]);
  return n;
}

u32 trimmedBlobSize(const TypeInfo& t, const void* obj, const void* defaults) {
  u32 end = blobSize(t);
  if (t.trimFloor == kNoTrim || !defaults) return end;
  const u8* a = static_cast<const u8*>(obj);
  const u8* d = static_cast<const u8*>(defaults);
  for (u32 i = t.fieldCount; i > t.trimFloor; --i) {
    const FieldDesc& f = t.fields[i - 1];
    if (!fieldEqual(f, a + f.offset, d + f.offset)) break;
    end -= fieldBlobSize(f);
  }
  return end;
}

void writeBlob(const TypeInfo& t, const void* obj, u8* out) {
  const u8* base = static_cast<const u8*>(obj);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    writeField(f, base + f.offset, out);
    out += fieldBlobSize(f);
  }
}

bool readBlob(const TypeInfo& t, void* obj, const u8* blob, u32 size) {
  // F5: fields are only ever appended, so a blob cooked before a field existed is shorter. It is accepted when it
  // ends exactly on a field boundary; the missing trailing fields keep the values already in `obj` (their defaults).
  u32 full = blobSize(t);
  if (size > full) return false;
  if (size < full) {
    u32 acc = 0;
    bool boundary = size == 0;
    for (u32 i = 0; i < t.fieldCount && acc < size; ++i) {
      acc += fieldBlobSize(t.fields[i]);
      if (acc == size) boundary = true;
    }
    if (!boundary) return false;
  }
  u8* base = static_cast<u8*>(obj);
  const u8* end = blob + size;
  for (u32 i = 0; i < t.fieldCount && blob < end; ++i) {
    const FieldDesc& f = t.fields[i];
    readField(f, base + f.offset, blob);
    blob += fieldBlobSize(f);
  }
  return true;
}

bool fieldsEqual(const TypeInfo& t, const void* a, const void* b) {
  const u8* pa = static_cast<const u8*>(a);
  const u8* pb = static_cast<const u8*>(b);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    if (!fieldEqual(f, pa + f.offset, pb + f.offset)) return false;
  }
  return true;
}

// State blob value: like the pack blob value, except an Array is prefixed with [u8 elemType][u16 cap] so the framing
// can be checked without the descriptor.
static u32 stateValueSize(const FieldDesc& f) { return f.type == FieldType::Array ? 3 + fieldBlobSize(f) : fieldBlobSize(f); }

u32 stateBlobSize(const TypeInfo& t) {
  u32 n = 4;
  for (u32 i = 0; i < t.fieldCount; ++i) n += 5 + stateValueSize(t.fields[i]);
  return n;
}

u32 saveState(const TypeInfo& t, const void* obj, u8* out, u32 cap) {
  u32 total = stateBlobSize(t);
  if (cap < total) return 0;
  memcpy(out, &t.fieldCount, 4);
  u8* p = out + 4;
  const u8* base = static_cast<const u8*>(obj);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    u32 h = fnv1a32(f.name);
    memcpy(p, &h, 4);
    p[4] = static_cast<u8>(f.type);
    u8* v = p + 5;
    if (f.type == FieldType::Array) {
      v[0] = static_cast<u8>(f.elemType);
      v[1] = static_cast<u8>(f.arrayCap & 0xFF);
      v[2] = static_cast<u8>(f.arrayCap >> 8);
      v += 3;
    }
    writeField(f, base + f.offset, v);
    p += 5 + stateValueSize(f);
  }
  return total;
}

// Length of the value of a state entry of type `ty` starting at `v` (`avail` bytes left); false if malformed.
static bool stateEntryLen(FieldType ty, const u8* v, u32 avail, u32* n) {
  if (ty == FieldType::Array) {
    if (avail < 3) return false;
    FieldType et = static_cast<FieldType>(v[0]);
    if (et != FieldType::Scalar && et != FieldType::I32 && et != FieldType::Guid && et != FieldType::Vector3) return false;
    u32 capacity = static_cast<u32>(v[1]) | (static_cast<u32>(v[2]) << 8);
    *n = 3 + 4 + capacity * fieldBlobSize(et);
    return *n <= avail;
  }
  *n = fieldBlobSize(ty);
  return *n <= avail;
}

int loadState(const TypeInfo& t, void* obj, const u8* data, u32 size) {
  if (size < 4) return -1;
  u32 count;
  memcpy(&count, data, 4);
  // First pass validates the framing so a bad blob leaves the object untouched.
  u32 off = 4;
  for (u32 i = 0; i < count; ++i) {
    if (size - off < 5 || data[off + 4] > static_cast<u8>(FieldType::Array)) return -1;
    u32 n;
    if (!stateEntryLen(static_cast<FieldType>(data[off + 4]), data + off + 5, size - off - 5, &n)) return -1;
    off += 5 + n;
  }
  if (off != size) return -1;
  u8* base = static_cast<u8*>(obj);
  int restored = 0;
  off = 4;
  for (u32 i = 0; i < count; ++i) {
    u32 h;
    memcpy(&h, data + off, 4);
    FieldType ty = static_cast<FieldType>(data[off + 4]);
    const u8* v = data + off + 5;
    u32 n = 0;
    stateEntryLen(ty, v, size - off - 5, &n);
    for (u32 k = 0; k < t.fieldCount; ++k) {
      const FieldDesc& f = t.fields[k];
      if (f.type != ty || fnv1a32(f.name) != h) continue;
      if (ty == FieldType::Array) {
        // Same element type: copy what fits (a recompiled capacity may differ).
        if (static_cast<u8>(f.elemType) != v[0]) break;
        const u32 srcCap = static_cast<u32>(v[1]) | (static_cast<u32>(v[2]) << 8);
        const u32 es = fieldBlobSize(f.elemType);
        u32 cnt;
        memcpy(&cnt, v + 3, 4);
        if (cnt > srcCap) cnt = srcCap;
        if (cnt > f.arrayCap) cnt = f.arrayCap;
        u8* dst = base + f.offset;
        memcpy(dst, &cnt, 4);
        memcpy(dst + 4, v + 7, cnt * es);
        memset(dst + 4 + cnt * es, 0, (f.arrayCap - cnt) * es);
      } else {
        readField(f, base + f.offset, v);
      }
      ++restored;
      break;
    }
    off += 5 + n;
  }
  return restored;
}

static int partCount(FieldType t) {
  switch (t) {
    case FieldType::Vector2: return 2;
    case FieldType::Vector3: return 3;
    case FieldType::Quaternion:
    case FieldType::Color32: return 4;
    default: return 0;
  }
}

static bool parsePart(FieldType t, const char* s, int* part) {
  if (!s[0] || s[1]) return false;
  const char* names = t == FieldType::Color32 ? "rgba" : "xyzw";
  for (int i = 0, n = partCount(t); i < n; ++i)
    if (names[i] == s[0]) {
      *part = i;
      return true;
    }
  return false;
}

bool parseFieldPath(const TypeInfo& t, const char* path, FieldPath& out) {
  out = FieldPath();
  if (!path || !*path) return false;
  const char* dot = strchr(path, '.');
  size_t len = dot ? static_cast<size_t>(dot - path) : strlen(path);
  const FieldDesc* f = nullptr;
  for (u32 i = 0; i < t.fieldCount; ++i)
    if (strlen(t.fields[i].name) == len && strncmp(t.fields[i].name, path, len) == 0) f = &t.fields[i];
  if (!f) return false;
  int part = -1, index = -1;
  if (dot) {
    const char* s = dot + 1;
    if (f->type == FieldType::Array) {
      if (*s < '0' || *s > '9') return false;
      long v = 0;
      while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        if (v >= f->arrayCap) return false;
        ++s;
      }
      index = static_cast<int>(v);
      if (*s == '.') {
        if (f->elemType != FieldType::Vector3 || !parsePart(f->elemType, s + 1, &part)) return false;
      } else if (*s) {
        return false;
      }
    } else if (!parsePart(f->type, s, &part)) {
      return false;
    }
  }
  out.field = f;
  out.part = part;
  out.index = index;
  return true;
}

}  // namespace rensga
