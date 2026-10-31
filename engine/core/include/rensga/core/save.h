// F2.05: save files. ISaveStore is the game-facing contract; FileSystemSaveStore keeps each save as one file
// "save:/<name>" on any IFileSystem (PC/Android/PSP: the save root; PS2: the memory card mc0:/RENSGA/).
// File layout: u32 magic 'RSAV', u32 payload size, u32 CRC-32 of the payload, payload. LoadFile rejects a file
// whose size or checksum does not match, so a torn write reads as "no save". No heap.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/feature.h"

namespace rensga {

class ISaveStore {
 public:
  virtual ~ISaveStore() {}
  // Names are short ASCII (letters, digits, '_', '-', '.'), at most 31 characters.
  virtual bool SaveFile(const char* name, const void* bytes, u32 size) = 0;
  // Returns the payload size copied into dst (at most cap), or -1 if the save is missing or damaged.
  virtual i32 LoadFile(const char* name, void* dst, u32 cap) = 0;
  virtual bool Exists(const char* name) = 0;
};

class FileSystemSaveStore : public ISaveStore {
 public:
  static constexpr u32 kMaxSave = 16 * 1024;  // scratch buffer size; saves are small (best scores, settings)
  explicit FileSystemSaveStore(IFileSystem& fs) : fs_(&fs) {}
  bool SaveFile(const char* name, const void* bytes, u32 size) override;
  i32 LoadFile(const char* name, void* dst, u32 cap) override;
  bool Exists(const char* name) override;

 private:
  bool path(const char* name, char* out, u32 outSize) const;
  IFileSystem* fs_;
  alignas(16) u8 scratch_[kMaxSave + 12];
};

// F5.15: where Save::SaveFile / LoadFile go. Auto = the save store (files). SystemDialog = the platform's own save dialog
// when it has one (PSP: sceUtilitySavedata); a platform without one falls back to Auto.
enum class SaveMode : u8 { Auto, SystemDialog };

// The save store scripts use (Save::SaveFile ...). The runtime main installs one; null means "no saves".
namespace Save {
void use(ISaveStore* store);
RENSGA_REQUIRES_FEATURE(SAVEDIALOG, "This profile has no system save dialog; keep the default Save::setMode(SaveMode::Auto).")
void setMode(SaveMode mode);
SaveMode mode();
ISaveStore* current();
bool SaveFile(const char* name, const void* bytes, u32 size);
i32 LoadFile(const char* name, void* dst, u32 cap);
bool Exists(const char* name);
// Convenience for a single i32 (best score, level reached). LoadInt returns `fallback` when missing or damaged.
bool SaveInt(const char* name, i32 value);
i32 LoadInt(const char* name, i32 fallback = 0);
}  // namespace Save

u32 crc32(const void* data, u32 size);

}  // namespace rensga
