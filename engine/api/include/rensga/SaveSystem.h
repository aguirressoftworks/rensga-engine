// F6 contract (C0): versioned save slots on top of the save store (rensga/core/save.h). A slot holds a version number, a payload
// and display metadata (title, detail line, icon texture GUID) that the platform shows in its own save browser (PS2 memory card
// icon.sys, PSP SAVEDATA PARAM.SFO). Read migrates old versions through a callback. Stub until A3 (Stage 1, API 9): Write fails,
// Read returns -1, Exists is false. Defined in engine/api/src/scene_api.cpp.
//
//   struct Save1 { i32 level; i32 coins; };           // version 1
//   SaveSystem::Write(0, 2, &data, sizeof(data));      // version 2 layout
//   i32 n = SaveSystem::Read(0, 2, &data, sizeof(data), &migrate);   // migrate(1, old, oldSize, out, cap, &outSize) upgrades v1
#pragma once
#include "rensga/core/guid.h"
#include "rensga/core/types.h"

namespace rensga {
namespace SaveSystem {
constexpr u32 kMaxSlots = 8;
constexpr u32 kMaxPayload = 16 * 1024;  // FileSystemSaveStore::kMaxSave minus the slot header

struct Metadata {
  char title[32] = {};   // shown in the platform's save browser
  char detail[32] = {};  // second line (level name, play time)
  Guid icon;             // texture asset; the platform module converts it (PS2 icon, PSP ICON0); null = the game's icon
};

struct SlotInfo {
  bool exists = false;
  u32 version = 0;
  u32 size = 0;  // payload bytes
  Metadata meta;
};

// Upgrades a payload written by `fromVersion` to the current layout into `out` (capacity `cap`); returns false if it cannot.
using MigrateFn = bool (*)(u32 fromVersion, const void* in, u32 inSize, void* out, u32 cap, u32* outSize);

bool Write(u32 slot, u32 version, const void* bytes, u32 size, const Metadata* meta = nullptr);
// Payload size copied into dst, or -1 if the slot is empty, damaged, newer than `version` or cannot be migrated.
i32 Read(u32 slot, u32 version, void* dst, u32 cap, MigrateFn migrate = nullptr);
bool Exists(u32 slot);
bool Delete(u32 slot);
bool GetInfo(u32 slot, SlotInfo* info);
}  // namespace SaveSystem
}  // namespace rensga
