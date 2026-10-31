// F6 contract (C0): Unity's PlayerPrefs, a small key/value store for settings (volume, difficulty, last level). Keys are short
// ASCII (at most 31 characters), strings at most 63. Capacity is fixed (kMaxEntries); a full store refuses new keys. Values live
// in memory and are written by Save() (also on a clean exit) through the save store (Save:: / SaveSystem), one slot file
// "prefs" per project, so they survive restarts on every platform. Stub until A3 (Stage 1, API 9): Set* do nothing, Get* return
// the default, HasKey is false. Defined in engine/api/src/scene_api.cpp.
#pragma once
#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {
namespace PlayerPrefs {
constexpr u32 kMaxEntries = 64;
constexpr u32 kMaxKeyLength = 31;
constexpr u32 kMaxStringLength = 63;

void SetInt(const char* key, i32 value);
i32 GetInt(const char* key, i32 defaultValue = 0);
void SetFloat(const char* key, Scalar value);
Scalar GetFloat(const char* key, Scalar defaultValue = 0);
void SetString(const char* key, const char* value);
// The returned text lives in a static buffer and stays valid until the next PlayerPrefs call.
const char* GetString(const char* key, const char* defaultValue = "");
bool HasKey(const char* key);
void DeleteKey(const char* key);
void DeleteAll();
bool Save();  // false when there is no save store or the write failed
}  // namespace PlayerPrefs
}  // namespace rensga
