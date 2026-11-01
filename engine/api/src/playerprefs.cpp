// F6 (A3, API 9): PlayerPrefs on top of the save store. One file "prefs" (Save::SaveFile) holds up to 64 entries. The table is read
// lazily on the first call after Save::use, written by Save() and when the game quits (Application::runQuitHooks). Fixed table, no heap.
#include <string.h>

#include "rensga/PlayerPrefs.h"
#include "rensga/core/application.h"
#include "rensga/core/log.h"
#include "rensga/core/save.h"

namespace rensga {

namespace {
constexpr u32 kMagic = 0x46455250u;  // 'P','R','E','F'
enum : u32 { kUnused = 0, kInt = 1, kFloat = 2, kString = 3 };
struct Entry {
  char key[PlayerPrefs::kMaxKeyLength + 1];
  u32 type;
  u8 value[PlayerPrefs::kMaxStringLength + 1];  // i32 or Scalar bits in the first 4 bytes, or the string
};
static_assert(sizeof(Entry) == 100, "Entry is 100 bytes on disk");
struct FileHead {
  u32 magic, version, count, reserved;
};

Entry g_entries[PlayerPrefs::kMaxEntries];
bool g_loaded = false;
bool g_dirty = false;
bool g_hooked = false;
ISaveStore* g_loadedFrom = nullptr;
char g_text[PlayerPrefs::kMaxStringLength + 1];

void flushHook() {
  if (g_dirty) PlayerPrefs::Save();
}

void load() {
  ISaveStore* store = Save::current();
  if (g_loaded && store == g_loadedFrom) return;
  g_loaded = true;
  g_loadedFrom = store;
  if (!g_hooked) {
    g_hooked = true;
    Application::addQuitHook(&flushHook);
  }
  if (!store) return;
  static u8 buf[sizeof(FileHead) + sizeof(g_entries)];
  const i32 n = Save::LoadFile("prefs", buf, sizeof(buf));
  if (n < static_cast<i32>(sizeof(FileHead))) return;
  FileHead h;
  memcpy(&h, buf, sizeof h);
  if (h.magic != kMagic || h.version != 1 || h.count > PlayerPrefs::kMaxEntries || sizeof(FileHead) + h.count * sizeof(Entry) > static_cast<u32>(n)) {
    RLOG_WARN("prefs", "the prefs file is damaged or from another version; starting empty");
    return;
  }
  memset(g_entries, 0, sizeof(g_entries));
  memcpy(g_entries, buf + sizeof(FileHead), h.count * sizeof(Entry));
  for (u32 i = 0; i < PlayerPrefs::kMaxEntries; ++i) {  // never trust text from a file
    g_entries[i].key[PlayerPrefs::kMaxKeyLength] = 0;
    g_entries[i].value[PlayerPrefs::kMaxStringLength] = 0;
  }
}

Entry* find(const char* key) {
  if (!key || !*key) return nullptr;
  for (u32 i = 0; i < PlayerPrefs::kMaxEntries; ++i)
    if (g_entries[i].type != kUnused && strcmp(g_entries[i].key, key) == 0) return &g_entries[i];
  return nullptr;
}

Entry* slotFor(const char* key) {
  if (!key || !*key || strlen(key) > PlayerPrefs::kMaxKeyLength) {
    RLOG_ERROR("prefs", "bad PlayerPrefs key \"%s\" (1 to %u characters)", key ? key : "(null)", PlayerPrefs::kMaxKeyLength);
    return nullptr;
  }
  if (Entry* e = find(key)) return e;
  for (u32 i = 0; i < PlayerPrefs::kMaxEntries; ++i) {
    if (g_entries[i].type != kUnused) continue;
    memset(&g_entries[i], 0, sizeof(Entry));
    strcpy(g_entries[i].key, key);
    return &g_entries[i];
  }
  RLOG_ERROR("prefs", "PlayerPrefs is full (%u keys); DeleteKey something first", PlayerPrefs::kMaxEntries);
  return nullptr;
}
}  // namespace

void PlayerPrefs::SetInt(const char* key, i32 value) {
  load();
  Entry* e = slotFor(key);
  if (!e) return;
  e->type = kInt;
  memcpy(e->value, &value, 4);
  g_dirty = true;
}
i32 PlayerPrefs::GetInt(const char* key, i32 defaultValue) {
  load();
  const Entry* e = find(key);
  if (!e) return defaultValue;
  i32 v = defaultValue;
  if (e->type == kInt) memcpy(&v, e->value, 4);
  else if (e->type == kFloat) { Scalar f; memcpy(&f, e->value, sizeof f); v = static_cast<i32>(f); }  // Unity returns the default for other types; the engine converts
  return v;
}
void PlayerPrefs::SetFloat(const char* key, Scalar value) {
  load();
  Entry* e = slotFor(key);
  if (!e) return;
  e->type = kFloat;
  memcpy(e->value, &value, sizeof value);
  g_dirty = true;
}
Scalar PlayerPrefs::GetFloat(const char* key, Scalar defaultValue) {
  load();
  const Entry* e = find(key);
  if (!e) return defaultValue;
  Scalar v = defaultValue;
  if (e->type == kFloat) memcpy(&v, e->value, sizeof v);
  else if (e->type == kInt) { i32 i; memcpy(&i, e->value, 4); v = static_cast<Scalar>(i); }
  return v;
}
void PlayerPrefs::SetString(const char* key, const char* value) {
  load();
  Entry* e = slotFor(key);
  if (!e) return;
  e->type = kString;
  memset(e->value, 0, sizeof(e->value));
  if (value) strncpy(reinterpret_cast<char*>(e->value), value, kMaxStringLength);
  g_dirty = true;
}
const char* PlayerPrefs::GetString(const char* key, const char* defaultValue) {
  load();
  const Entry* e = find(key);
  const char* src = (e && e->type == kString) ? reinterpret_cast<const char*>(e->value) : (defaultValue ? defaultValue : "");
  strncpy(g_text, src, kMaxStringLength);
  g_text[kMaxStringLength] = 0;
  return g_text;
}
bool PlayerPrefs::HasKey(const char* key) {
  load();
  return find(key) != nullptr;
}
void PlayerPrefs::DeleteKey(const char* key) {
  load();
  if (Entry* e = find(key)) {
    memset(e, 0, sizeof(Entry));
    g_dirty = true;
  }
}
void PlayerPrefs::DeleteAll() {
  load();
  memset(g_entries, 0, sizeof(g_entries));
  g_dirty = true;
}
bool PlayerPrefs::Save() {
  load();
  ISaveStore* store = rensga::Save::current();
  if (!store) return false;
  static u8 buf[sizeof(FileHead) + sizeof(g_entries)];
  FileHead h = {kMagic, 1, 0, 0};
  u8* out = buf + sizeof(FileHead);
  for (u32 i = 0; i < kMaxEntries; ++i) {
    if (g_entries[i].type == kUnused) continue;
    memcpy(out, &g_entries[i], sizeof(Entry));
    out += sizeof(Entry);
    ++h.count;
  }
  memcpy(buf, &h, sizeof h);
  const bool ok = rensga::Save::SaveFile("prefs", buf, static_cast<u32>(out - buf));
  if (ok) g_dirty = false;
  return ok;
}

}  // namespace rensga
