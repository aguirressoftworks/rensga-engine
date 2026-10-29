// Basic types shared by every Rensga module. No console-specific code here.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rensga {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

constexpr u32 kInvalidIndex = 0xFFFFFFFFu;
constexpr size_t kDmaAlignment = 16;  // RULES section 3

// Non-owning view of contiguous memory.
template <class T>
struct Span {
  T* data = nullptr;
  size_t size = 0;
  const T* begin() const { return data; }
  const T* end() const { return data + size; }
  T* begin() { return data; }
  T* end() { return data + size; }
  bool empty() const { return size == 0; }
  explicit operator bool() const { return data != nullptr; }
};

constexpr u32 fnv1a32(const char* s) {
  u32 h = 2166136261u;
  while (*s) {
    h ^= static_cast<u8>(*s++);
    h *= 16777619u;
  }
  return h;
}

constexpr size_t alignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

}  // namespace rensga

#if defined(__GNUC__)
#define RENSGA_PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))
#else
#define RENSGA_PRINTF_LIKE(f, a)
#endif
#define RENSGA_CAT2(a, b) a##b
#define RENSGA_CAT(a, b) RENSGA_CAT2(a, b)
