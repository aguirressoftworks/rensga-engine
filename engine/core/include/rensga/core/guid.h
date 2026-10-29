// MVP-14: 128-bit GUID, text form "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (lowercase hex).
// Every asset and object reference uses it (RULES 1.7).
#pragma once
#include "rensga/core/types.h"

namespace rensga {

struct Guid {
  u8 bytes[16] = {};  // text order == byte order

  static constexpr size_t kTextLength = 36;  // without NUL
  bool isNull() const {
    for (u8 b : bytes)
      if (b) return false;
    return true;
  }
  bool operator==(const Guid& o) const {
    for (int i = 0; i < 16; ++i)
      if (bytes[i] != o.bytes[i]) return false;
    return true;
  }
  bool operator!=(const Guid& o) const { return !(*this == o); }
  bool operator<(const Guid& o) const {
    for (int i = 0; i < 16; ++i)
      if (bytes[i] != o.bytes[i]) return bytes[i] < o.bytes[i];
    return false;
  }

  // Parse exactly 36 chars (hex either case). Returns false and leaves out null on failure.
  static bool parse(const char* text, Guid& out);
  // Writes 36 chars plus NUL; buffer must hold 37.
  void format(char* out37) const;
  // Deterministic 32-bit hash for tables.
  u32 hash() const;
};

static_assert(sizeof(Guid) == 16, "Guid must be 16 bytes (pack format)");

}  // namespace rensga
