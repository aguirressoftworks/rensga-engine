#include "rensga/core/reflect.h"

#include <string.h>

#include "rensga/core/log.h"

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

namespace reflect {

PendingMods& pending() {
  static PendingMods p;
  return p;
}

void addField(TypeInfo* info, const char* name, FieldType type, u32 offset, u32 size) {
  PendingMods& p = pending();
  if (!info) {
    p = PendingMods();
    return;
  }
  if (info->fieldCount >= kMaxFields) {
    RLOG_ERROR("reflect", "type '%s' has more than %u fields", info->name, kMaxFields);
    p = PendingMods();
    return;
  }
  FieldDesc& f = info->fields[info->fieldCount++];
  f.name = name;
  f.type = type;
  f.offset = offset;
  f.size = size;
  f.hasRange = p.hasRange;
  f.rangeMin = p.min;
  f.rangeMax = p.max;
  f.header = p.header;
  p = PendingMods();
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

}  // namespace rensga
