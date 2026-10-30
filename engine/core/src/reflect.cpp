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
  }
  return 0;
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
  }
  return "?";
}

TypeRegistry& TypeRegistry::instance() {
  static TypeRegistry r;
  return r;
}

const TypeInfo* TypeRegistry::find(const char* name) const {
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

u32 blobSize(const TypeInfo& t) {
  u32 n = 0;
  for (u32 i = 0; i < t.fieldCount; ++i) n += fieldBlobSize(t.fields[i].type);
  return n;
}

void writeBlob(const TypeInfo& t, const void* obj, u8* out) {
  const u8* base = static_cast<const u8*>(obj);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    u32 n = fieldBlobSize(f.type);
    const u8* src = base + f.offset;
    if (f.type == FieldType::Color32) {
      const Color32* c = reinterpret_cast<const Color32*>(src);
      out[0] = c->r; out[1] = c->g; out[2] = c->b; out[3] = c->a;
    } else {
      memcpy(out, src, n);
    }
    out += n;
  }
}

bool readBlob(const TypeInfo& t, void* obj, const u8* blob, u32 size) {
  if (size != blobSize(t)) return false;
  u8* base = static_cast<u8*>(obj);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    u32 n = fieldBlobSize(f.type);
    u8* dst = base + f.offset;
    if (f.type == FieldType::Color32) {
      *reinterpret_cast<Color32*>(dst) = Color32(blob[0], blob[1], blob[2], blob[3]);
    } else {
      memcpy(dst, blob, n);
    }
    blob += n;
  }
  return true;
}

bool fieldsEqual(const TypeInfo& t, const void* a, const void* b) {
  const u8* pa = static_cast<const u8*>(a);
  const u8* pb = static_cast<const u8*>(b);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    // Compare value bytes of the in-memory representation (all field types are plain data).
    if (memcmp(pa + f.offset, pb + f.offset, fieldBlobSize(f.type)) != 0) return false;
  }
  return true;
}

u32 stateBlobSize(const TypeInfo& t) {
  u32 n = 4;
  for (u32 i = 0; i < t.fieldCount; ++i) n += 5 + fieldBlobSize(t.fields[i].type);
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
    u32 h = fnv1a32(f.name), n = fieldBlobSize(f.type);
    memcpy(p, &h, 4);
    p[4] = static_cast<u8>(f.type);
    if (f.type == FieldType::Color32) {
      const Color32* c = reinterpret_cast<const Color32*>(base + f.offset);
      p[5] = c->r; p[6] = c->g; p[7] = c->b; p[8] = c->a;
    } else {
      memcpy(p + 5, base + f.offset, n);
    }
    p += 5 + n;
  }
  return total;
}

int loadState(const TypeInfo& t, void* obj, const u8* data, u32 size) {
  if (size < 4) return -1;
  u32 count;
  memcpy(&count, data, 4);
  // First pass validates the framing so a bad blob leaves the object untouched.
  u32 off = 4;
  for (u32 i = 0; i < count; ++i) {
    if (size - off < 5 || data[off + 4] > static_cast<u8>(FieldType::UnityEvent)) return -1;
    u32 n = fieldBlobSize(static_cast<FieldType>(data[off + 4]));
    if (size - off - 5 < n) return -1;
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
    u32 n = fieldBlobSize(ty);
    for (u32 k = 0; k < t.fieldCount; ++k) {
      const FieldDesc& f = t.fields[k];
      if (f.type != ty || fnv1a32(f.name) != h) continue;
      const u8* v = data + off + 5;
      if (ty == FieldType::Color32) *reinterpret_cast<Color32*>(base + f.offset) = Color32(v[0], v[1], v[2], v[3]);
      else memcpy(base + f.offset, v, n);
      ++restored;
      break;
    }
    off += 5 + n;
  }
  return restored;
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
  int part = -1;
  if (dot) {
    const char* s = dot + 1;
    if (!s[0] || s[1]) return false;
    int cnt = f->type == FieldType::Vector3 ? 3 : f->type == FieldType::Quaternion ? 4 : f->type == FieldType::Color32 ? 4 : 0;
    const char* names = f->type == FieldType::Color32 ? "rgba" : "xyzw";
    for (int i = 0; i < cnt; ++i)
      if (names[i] == s[0]) part = i;
    if (part < 0) return false;
  }
  out.field = f;
  out.part = part;
  return true;
}

}  // namespace rensga
