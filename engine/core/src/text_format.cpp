// F6.12 (D1): Format() for UI text. No heap, no printf: the same characters on every platform (text_format.h).
#include "rensga/core/text_format.h"

namespace rensga {

namespace {
// One argument rendered with an optional precision override (-1 = the argument's own).
void render(StrBuf<48>& out, const FmtArg& a, int precision) {
  switch (a.kind) {
    case FmtArg::Int: out.Append(a.i); break;
    case FmtArg::Uint: out.Append(a.u); break;
    case FmtArg::Real: out.Append(Fixed(a.s, precision >= 0 ? precision : a.decimals)); break;
    case FmtArg::Bool: out.Append(a.i != 0); break;
    case FmtArg::Char: out.Append(static_cast<char>(a.i)); break;
    case FmtArg::Str: out.Append(a.str); break;
    case FmtArg::None: break;
  }
}
}  // namespace

u32 FormatArgs(char* buf, u32 size, const char* fmt, const FmtArg* args, u32 count) {
  if (!buf || size == 0) return 0;
  u32 n = 0;
  auto put = [&](char c) { if (n + 1 < size) buf[n++] = c; };
  for (const char* p = fmt; p && *p; ++p) {
    if (p[0] == '{' && p[1] == '{') { put('{'); ++p; continue; }
    if (p[0] == '}' && p[1] == '}') { put('}'); ++p; continue; }
    if (p[0] == '{' && p[1] >= '0' && p[1] <= '9') {
      const char* q = p + 2;
      bool zero = false;
      int width = 0, precision = -1;
      bool ok = true;
      if (*q == ':') {
        ++q;
        if (*q == '0') { zero = true; ++q; }
        while (*q >= '0' && *q <= '9') { width = width * 10 + (*q - '0'); ++q; }
        if (*q == '.') {
          ++q;
          precision = 0;
          while (*q >= '0' && *q <= '9') { precision = precision * 10 + (*q - '0'); ++q; }
          if (precision > 6) precision = 6;
        }
      }
      if (*q != '}') ok = false;
      if (ok) {
        const u32 idx = static_cast<u32>(p[1] - '0');
        StrBuf<48> piece;
        if (idx < count) render(piece, args[idx], precision);
        const u32 len = piece.size();
        const char* text = piece.c_str();
        u32 padN = width > 0 && static_cast<u32>(width) > len ? static_cast<u32>(width) - len : 0;
        u32 skip = 0;
        if (zero && padN && (text[0] == '-' || text[0] == '+') && args[idx < count ? idx : 0].kind != FmtArg::Str) { put(text[0]); skip = 1; }
        for (; padN; --padN) put(zero && idx < count && args[idx].kind != FmtArg::Str ? '0' : ' ');
        for (const char* t = text + skip; *t; ++t) put(*t);
        p = q;
        continue;
      }
    }
    put(*p);
  }
  buf[n] = 0;
  return n;
}

}  // namespace rensga
