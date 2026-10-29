#include "rensga/core/guid.h"

namespace rensga {

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool Guid::parse(const char* text, Guid& out) {
  out = Guid();
  if (!text) return false;
  Guid g;
  int nibble = 0;
  for (size_t i = 0; i < kTextLength; ++i) {
    char c = text[i];
    if (c == 0) return false;
    bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
    if (dash) {
      if (c != '-') return false;
      continue;
    }
    int v = hexVal(c);
    if (v < 0) return false;
    if (nibble & 1)
      g.bytes[nibble / 2] = static_cast<u8>(g.bytes[nibble / 2] | v);
    else
      g.bytes[nibble / 2] = static_cast<u8>(v << 4);
    ++nibble;
  }
  if (text[kTextLength] != 0) return false;
  out = g;
  return true;
}

void Guid::format(char* out) const {
  static const char* hex = "0123456789abcdef";
  size_t o = 0;
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
    out[o++] = hex[bytes[i] >> 4];
    out[o++] = hex[bytes[i] & 15];
  }
  out[o] = 0;
}

u32 Guid::hash() const {
  u32 h = 2166136261u;
  for (u8 b : bytes) {
    h ^= b;
    h *= 16777619u;
  }
  return h;
}

}  // namespace rensga
