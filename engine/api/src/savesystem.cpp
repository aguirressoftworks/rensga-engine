// F6 (A3, API 9): versioned save slots on the save store. Slot n is the store file "slotN". Payload layout inside the store's own
// {magic, size, crc} wrapper (a torn write reads as "no save"):
//   u32 'SLOT', u32 version (0 = deleted), u32 payload size, u32 reserved, char title[32], char detail[32], Guid icon, payload.
// The store file names are the same on every platform (PC and Android: files; PS2: mc0:/RENSGA/slotN with the game's icon.sys; PSP:
// ms0 files, or SAVEDATA/<GAMEID>SLOTN through Save::setMode(SystemDialog)). The title and detail are also handed to the platform
// (IPlatform::setSaveMetadata) so the PSP save browser shows them. Delete writes a "deleted" header: the store has no remove.
#include <stdio.h>
#include <string.h>

#include "rensga/SaveSystem.h"
#include "rensga/core/log.h"
#include "rensga/core/save.h"
#include "rensga/core/system.h"

namespace rensga {

namespace {
constexpr u32 kMagic = 0x544F4C53u;  // 'S','L','O','T'
struct SlotHeader {
  u32 magic, version, size, reserved;
  char title[32];
  char detail[32];
  Guid icon;
};
static_assert(sizeof(SlotHeader) == 96, "SlotHeader is 96 bytes");
alignas(16) u8 g_buf[sizeof(SlotHeader) + SaveSystem::kMaxPayload];

bool nameOf(u32 slot, char* out, u32 size) {
  if (slot >= SaveSystem::kMaxSlots) return false;
  snprintf(out, size, "slot%u", static_cast<unsigned>(slot));
  return true;
}

// Reads the whole slot file into g_buf. Returns the bytes, or -1.
i32 readFile(u32 slot, SlotHeader* h) {
  char name[16];
  if (!nameOf(slot, name, sizeof(name)) || !Save::current()) return -1;
  const i32 n = Save::LoadFile(name, g_buf, sizeof(g_buf));
  if (n < static_cast<i32>(sizeof(SlotHeader))) return -1;
  memcpy(h, g_buf, sizeof(SlotHeader));
  h->title[31] = 0;
  h->detail[31] = 0;
  if (h->magic != kMagic || h->size > static_cast<u32>(n) - sizeof(SlotHeader)) return -1;
  return n;
}
}  // namespace

bool SaveSystem::Write(u32 slot, u32 version, const void* bytes, u32 size, const Metadata* meta) {
  char name[16];
  if (!nameOf(slot, name, sizeof(name))) {
    RLOG_ERROR("save", "SaveSystem::Write: slot %u is out of range (%u slots)", slot, kMaxSlots);
    return false;
  }
  if (version == 0 || size > kMaxPayload || (!bytes && size)) {
    RLOG_ERROR("save", "SaveSystem::Write: version must be 1 or more and the payload at most %u bytes (got version %u, %u bytes)", kMaxPayload, version, size);
    return false;
  }
  if (!Save::current() && Save::mode() == SaveMode::Auto) return false;
  SlotHeader h = {};
  h.magic = kMagic;
  h.version = version;
  h.size = size;
  if (meta) {
    memcpy(h.title, meta->title, sizeof(h.title));
    memcpy(h.detail, meta->detail, sizeof(h.detail));
    h.icon = meta->icon;
    h.title[31] = 0;
    h.detail[31] = 0;
  }
  memcpy(g_buf, &h, sizeof h);
  if (size) memcpy(g_buf + sizeof h, bytes, size);
  if (IPlatform* p = system_services::platform()) p->setSaveMetadata(h.title, h.detail);
  return Save::SaveFile(name, g_buf, static_cast<u32>(sizeof h) + size);
}

i32 SaveSystem::Read(u32 slot, u32 version, void* dst, u32 cap, MigrateFn migrate) {
  SlotHeader h;
  if (readFile(slot, &h) < 0 || h.version == 0) return -1;
  const u8* payload = g_buf + sizeof(SlotHeader);
  if (h.version == version) {
    if (h.size > cap) return -1;
    if (dst && h.size) memcpy(dst, payload, h.size);
    return static_cast<i32>(h.size);
  }
  if (h.version > version) {
    RLOG_WARN("save", "slot %u was written by a newer version (%u, this game reads %u)", slot, h.version, version);
    return -1;
  }
  if (!migrate || !dst) return -1;
  u32 outSize = 0;
  if (!migrate(h.version, payload, h.size, dst, cap, &outSize) || outSize > cap) {
    RLOG_WARN("save", "slot %u: migration from version %u failed", slot, h.version);
    return -1;
  }
  return static_cast<i32>(outSize);
}

bool SaveSystem::Exists(u32 slot) {
  SlotHeader h;
  return readFile(slot, &h) >= 0 && h.version != 0;
}

bool SaveSystem::Delete(u32 slot) {
  char name[16];
  if (!nameOf(slot, name, sizeof(name)) || !Save::current()) return false;
  if (!Save::Exists(name)) return true;
  SlotHeader h = {};
  h.magic = kMagic;
  return Save::SaveFile(name, &h, sizeof h);
}

bool SaveSystem::GetInfo(u32 slot, SlotInfo* info) {
  if (info) *info = SlotInfo();
  SlotHeader h;
  if (readFile(slot, &h) < 0 || h.version == 0) return false;
  if (info) {
    info->exists = true;
    info->version = h.version;
    info->size = h.size;
    memcpy(info->meta.title, h.title, sizeof(h.title));
    memcpy(info->meta.detail, h.detail, sizeof(h.detail));
    info->meta.icon = h.icon;
  }
  return true;
}

}  // namespace rensga
