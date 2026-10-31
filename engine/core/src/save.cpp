#include "rensga/core/save.h"

#include <stdio.h>
#include <string.h>

#include "rensga/core/log.h"
#include "rensga/core/system.h"

namespace rensga {

namespace {
constexpr u32 kMagic = 0x56415352u;  // 'R','S','A','V' little-endian
ISaveStore* g_store = nullptr;
SaveMode g_mode = SaveMode::Auto;

void put32(u8* p, u32 v) { p[0] = static_cast<u8>(v); p[1] = static_cast<u8>(v >> 8); p[2] = static_cast<u8>(v >> 16); p[3] = static_cast<u8>(v >> 24); }
u32 get32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32>(p[3]) << 24); }
}  // namespace

u32 crc32(const void* data, u32 size) {
  const u8* p = static_cast<const u8*>(data);
  u32 c = 0xFFFFFFFFu;
  for (u32 i = 0; i < size; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

bool FileSystemSaveStore::path(const char* name, char* out, u32 outSize) const {
  if (!name || !*name || strlen(name) > 31) return false;
  for (const char* p = name; *p; ++p) {
    const char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
  }
  if (name[0] == '.') return false;
  int n = snprintf(out, outSize, "save:/%s", name);
  return n > 0 && static_cast<u32>(n) < outSize;
}

bool FileSystemSaveStore::SaveFile(const char* name, const void* bytes, u32 size) {
  char full[64];
  if (!path(name, full, sizeof(full)) || size > kMaxSave || (!bytes && size)) {
    RLOG_ERROR("save", "SaveFile: bad name or size (%s, %u bytes)", name ? name : "(null)", size);
    return false;
  }
  put32(scratch_, kMagic);
  put32(scratch_ + 4, size);
  put32(scratch_ + 8, crc32(bytes, size));
  if (size) memcpy(scratch_ + 12, bytes, size);
  const bool ok = fs_->writeFile(full, scratch_, size + 12);
  if (!ok) RLOG_ERROR("save", "SaveFile: cannot write %s", full);
  return ok;
}

i32 FileSystemSaveStore::LoadFile(const char* name, void* dst, u32 cap) {
  char full[64];
  if (!path(name, full, sizeof(full))) return -1;
  const i32 h = fs_->openStream(full);
  if (h < 0) return -1;
  const u32 got = fs_->readStream(h, scratch_, sizeof(scratch_));
  fs_->closeStream(h);
  if (got < 12 || get32(scratch_) != kMagic) return -1;
  const u32 size = get32(scratch_ + 4);
  if (size > kMaxSave || got != size + 12 || crc32(scratch_ + 12, size) != get32(scratch_ + 8)) return -1;
  const u32 n = size < cap ? size : cap;
  if (dst && n) memcpy(dst, scratch_ + 12, n);
  return static_cast<i32>(n);
}

bool FileSystemSaveStore::Exists(const char* name) {
  char full[64];
  if (!path(name, full, sizeof(full))) return false;
  const i32 h = fs_->openStream(full);
  if (h < 0) return false;
  fs_->closeStream(h);
  return true;
}

void Save::use(ISaveStore* store) { g_store = store; }
ISaveStore* Save::current() { return g_store; }
void Save::setMode(SaveMode mode) { g_mode = mode; }
SaveMode Save::mode() { return g_mode; }
bool Save::SaveFile(const char* name, const void* bytes, u32 size) {
  if (g_mode == SaveMode::SystemDialog) {
    if (IPlatform* p = system_services::platform()) {
      const i32 r = p->saveDialog(name, const_cast<void*>(bytes), size, false);
      if (r >= 0) return r > 0;  // -1 = no dialog here: fall back to the store
    }
  }
  return g_store && g_store->SaveFile(name, bytes, size);
}
i32 Save::LoadFile(const char* name, void* dst, u32 cap) {
  if (g_mode == SaveMode::SystemDialog) {
    if (IPlatform* p = system_services::platform()) {
      const i32 r = p->saveDialog(name, dst, cap, true);
      if (r >= 0) return r > 0 ? r : -1;
    }
  }
  return g_store ? g_store->LoadFile(name, dst, cap) : -1;
}
bool Save::Exists(const char* name) { return g_store && g_store->Exists(name); }
bool Save::SaveInt(const char* name, i32 value) {
  u8 b[4];
  put32(b, static_cast<u32>(value));
  return SaveFile(name, b, 4);
}
i32 Save::LoadInt(const char* name, i32 fallback) {
  u8 b[4];
  return LoadFile(name, b, 4) == 4 ? static_cast<i32>(get32(b)) : fallback;
}

}  // namespace rensga
