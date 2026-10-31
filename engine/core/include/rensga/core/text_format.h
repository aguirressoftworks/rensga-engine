// F6 contract (C0): allocation-free text formatting for UI text and logs. No heap, no printf, no locale: the same characters on
// every platform (Scalars print with a fixed number of decimals, rounded half away from zero).
//
//   StrBuf<32> s;  s << "Score " << score << "  x" << multiplier;      // appends; overflow cuts the text, never writes past N
//   label->SetText(s.c_str());
//   char buf[32];  Format(buf, sizeof(buf), "HP {0}/{1}", hp, maxHp);   // {0} {1} ... name the arguments (any order, any number of times); {{ and }} are braces
// Arguments: i32, u32, Scalar (2 decimals; Fixed(value, n) picks n = 0..6), bool ("true" / "false"), const char*, char.
#pragma once
#include <string.h>

#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {

// A Scalar printed with `decimals` digits after the point.
struct Fixed {
  Scalar value;
  int decimals;
  Fixed(Scalar v, int d = 2) : value(v), decimals(d < 0 ? 0 : (d > 6 ? 6 : d)) {}
};

template <u32 N>
class StrBuf {
  static_assert(N >= 2, "StrBuf needs room for a NUL");

 public:
  StrBuf() { s_[0] = 0; }
  const char* c_str() const { return s_; }
  u32 size() const { return len_; }
  bool truncated() const { return truncated_; }
  void Clear() { len_ = 0; s_[0] = 0; truncated_ = false; }
  operator const char*() const { return s_; }

  StrBuf& Append(const char* t) {
    for (; t && *t; ++t) Put(*t);
    return *this;
  }
  StrBuf& Append(char c) { Put(c); return *this; }
  StrBuf& Append(bool b) { return Append(b ? "true" : "false"); }
  StrBuf& Append(i32 v) {
    if (v < 0) { Put('-'); return AppendU(0u - static_cast<u32>(v)); }
    return AppendU(static_cast<u32>(v));
  }
  StrBuf& Append(u32 v) { return AppendU(v); }
  StrBuf& Append(Scalar v) { return Append(Fixed(v, 2)); }
  StrBuf& Append(const Fixed& f) {
    double d = static_cast<double>(f.value);  // host double only for printing; Scalar fixed point converts the same way
    bool neg = d < 0;
    if (neg) d = -d;
    u64 scale = 1;
    for (int i = 0; i < f.decimals; ++i) scale *= 10;
    u64 scaled = static_cast<u64>(d * static_cast<double>(scale) + 0.5);
    if (neg && scaled != 0) Put('-');
    AppendU(static_cast<u32>(scaled / scale));
    if (f.decimals > 0) {
      Put('.');
      u64 frac = scaled % scale;
      for (u64 p = scale / 10; p > 0; p /= 10) Put(static_cast<char>('0' + (frac / p) % 10));
    }
    return *this;
  }
  template <class T>
  StrBuf& operator<<(const T& v) { return Append(v); }
  StrBuf& operator<<(const char* v) { return Append(v); }

 private:
  void Put(char c) {
    if (len_ + 1 >= N) { truncated_ = true; return; }
    s_[len_++] = c;
    s_[len_] = 0;
  }
  StrBuf& AppendU(u32 v) {
    char tmp[10];
    int n = 0;
    do { tmp[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    while (n) Put(tmp[--n]);
    return *this;
  }
  char s_[N];
  u32 len_ = 0;
  bool truncated_ = false;
};

// Writes `fmt` with {0}.. replaced by the arguments into buf (NUL terminated, cut to `size`). Returns the length written.
template <class... Args>
u32 Format(char* buf, u32 size, const char* fmt, const Args&... args) {
  if (!buf || size == 0) return 0;
  // Render every argument to its own small buffer first, so {n} can be used in any order and more than once.
  constexpr u32 kArgs = sizeof...(Args) ? sizeof...(Args) : 1;
  StrBuf<32> parts[kArgs];  // an argument longer than 31 characters is cut
  u32 i = 0;
  (void)i;
  int dummy[] = {0, (parts[i++].Append(args), 0)...};
  (void)dummy;
  u32 n = 0;
  auto put = [&](char c) { if (n + 1 < size) buf[n++] = c; };
  for (const char* p = fmt; p && *p; ++p) {
    if (p[0] == '{' && p[1] == '{') { put('{'); ++p; continue; }
    if (p[0] == '}' && p[1] == '}') { put('}'); ++p; continue; }
    if (p[0] == '{' && p[1] >= '0' && p[1] <= '9' && p[2] == '}') {
      u32 idx = static_cast<u32>(p[1] - '0');
      if (idx < sizeof...(Args)) for (const char* q = parts[idx].c_str(); *q; ++q) put(*q);
      p += 2;
      continue;
    }
    put(*p);
  }
  buf[n] = 0;
  return n;
}

}  // namespace rensga
