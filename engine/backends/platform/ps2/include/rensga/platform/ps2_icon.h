// F4.01: the PS2 memory card save icon, built in code (no files in the repository, no Sony assets).
//
// A save folder shows up in the BIOS browser with the icon described by two files next to the data:
//   mc0:/RENSGA/icon.sys    964 bytes, the folder's title, colours, lights and the names of the icon files (layout below)
//   mc0:/RENSGA/rensga.ico  the 3D icon: a cube with a 128x128 texture, one animation shape
// Header only and free of PS2 includes so the byte layout is unit-checked on the host (tests/sample/test_ps2_icon.cpp).
// No heap: the caller owns the buffers. Ps2Platform writes both files once when it opens the card (ps2_platform.cpp).
#pragma once
#include <string.h>

namespace rensga {
namespace ps2icon {

// ---- icon.sys -----------------------------------------------------------------------------------------------------------------
// offset  size  field
//   0x00     4  "PS2D"
//   0x04     2  reserved (0)
//   0x06     2  byte offset in `title` where the second line starts
//   0x08     4  reserved (0)
//   0x0C     4  background transparency, 0..128
//   0x10    64  4 background corner colours, each 4 x i32 (R, G, B, 0), 0..255
//   0x50    48  3 light directions, each 4 x f32
//   0x80    48  3 light colours, each 4 x f32 (R, G, B, 0), 0..1
//   0xB0    16  ambient colour 4 x f32
//   0xC0    68  title, Shift-JIS (plain ASCII bytes are valid), zero padded; the two lines are stored back to back
//   0x104   64  file name of the icon shown in the browser
//   0x144   64  file name of the icon shown while copying
//   0x184   64  file name of the icon shown while deleting
//   0x1C4  512  reserved (0)
constexpr unsigned kIconSysSize = 964;
constexpr unsigned kOffTitle = 0xC0, kOffView = 0x104, kOffCopy = 0x144, kOffDelete = 0x184;

inline void putU16(unsigned char* p, unsigned v) {
  p[0] = static_cast<unsigned char>(v);
  p[1] = static_cast<unsigned char>(v >> 8);
}
inline void putU32(unsigned char* p, unsigned v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
inline void putF32(unsigned char* p, float f) {
  unsigned v;
  memcpy(&v, &f, 4);  // little endian on the EE and on every host this runs on
  putU32(p, v);
}
inline void copyName(unsigned char* dst, unsigned cap, const char* s) {
  unsigned n = 0;
  for (; s && s[n] && n + 1 < cap; ++n) dst[n] = static_cast<unsigned char>(s[n]);
}

// line1 + line2 must fit 68 bytes (including the terminator). Returns false if they do not or `iconFile` does not fit 64 bytes.
inline bool buildIconSys(unsigned char* out, const char* line1, const char* line2, const char* iconFile) {
  const unsigned n1 = static_cast<unsigned>(strlen(line1)), n2 = static_cast<unsigned>(strlen(line2));
  if (n1 + n2 + 1 > 68 || strlen(iconFile) + 1 > 64) return false;
  memset(out, 0, kIconSysSize);
  memcpy(out, "PS2D", 4);
  putU16(out + 0x06, n1);
  putU32(out + 0x0C, 64);
  static const int bg[4][3] = {{0xF2, 0x9A, 0x1E}, {0xC8, 0x5A, 0x14}, {0xF8, 0xC8, 0x3C}, {0x8C, 0x3C, 0x10}};  // pequi: gold to rust
  for (int i = 0; i < 4; ++i)
    for (int c = 0; c < 3; ++c) putU32(out + 0x10 + 16 * i + 4 * c, static_cast<unsigned>(bg[i][c]));
  static const float dir[3][3] = {{0.5f, 0.5f, 0.5f}, {0.0f, -0.4f, -0.1f}, {-0.5f, -0.5f, 0.5f}};
  static const float col[3][3] = {{0.6f, 0.6f, 0.6f}, {0.3f, 0.3f, 0.3f}, {0.2f, 0.2f, 0.2f}};
  for (int i = 0; i < 3; ++i)
    for (int c = 0; c < 3; ++c) {
      putF32(out + 0x50 + 16 * i + 4 * c, dir[i][c]);
      putF32(out + 0x80 + 16 * i + 4 * c, col[i][c]);
    }
  for (int c = 0; c < 3; ++c) putF32(out + 0xB0 + 4 * c, 0.4f);
  copyName(out + kOffTitle, n1 + 1, line1);
  copyName(out + kOffTitle + n1, n2 + 1, line2);
  copyName(out + kOffView, 64, iconFile);
  copyName(out + kOffCopy, 64, iconFile);
  copyName(out + kOffDelete, 64, iconFile);
  return true;
}

// ---- the 3D icon ----------------------------------------------------------------------------------------------------------------
// header   u32 magic 0x00010000, u32 animation shapes (1), u32 texture type (7 = uncompressed texture), u32 float 1.0, u32 vertex count (36)
// vertices per vertex: 4 x i16 position (x, y, z, 0; 4.12 fixed), 4 x i16 normal, 2 x i16 texture (u, v; 0..4096), 4 x u8 colour (RGBA)
// animation u32 id (1), u32 frame length, f32 speed, u32 play offset, u32 frame count, then per frame: u32 shape id, u32 key count,
//           key count x (f32 time, f32 value)
// texture  128 x 128 x u16, bit15 = alpha, then 5 bits each of blue, green, red (low bits)
constexpr unsigned kIconMagic = 0x00010000u, kIconVertices = 36, kIconTexW = 128, kIconTexH = 128;
constexpr unsigned kIconHeaderSize = 20, kIconVertexSize = 8 + 8 + 4 + 4;                 // 24 bytes
constexpr unsigned kIconAnimSize = 20 + 8 + 2 * 8;                                          // header + 1 frame + 2 keys = 44
constexpr unsigned kIconFileSize = kIconHeaderSize + kIconVertices * kIconVertexSize + kIconAnimSize + kIconTexW * kIconTexH * 2;

inline unsigned short rgb5551(unsigned r, unsigned g, unsigned b) {
  return static_cast<unsigned short>(0x8000u | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
}

// A 5x7 letter R scaled 12x on a pequi gradient: the icon is the engine's gold, no Sony colours or shapes.
inline void buildIconTexture(unsigned char* out) {
  static const unsigned char rows[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};   // bit 4 = leftmost column
  for (unsigned y = 0; y < kIconTexH; ++y)
    for (unsigned x = 0; x < kIconTexW; ++x) {
      unsigned r = 250 - y / 3, g = 180 - y / 2, b = 40;
      const int lx = static_cast<int>(x) - 34, ly = static_cast<int>(y) - 22;   // letter box 60 x 84
      if (lx >= 0 && ly >= 0 && lx < 60 && ly < 84 && ((rows[ly / 12] >> (4 - lx / 12)) & 1)) { r = 48; g = 28; b = 16; }
      putU16(out + 2 * (y * kIconTexW + x), rgb5551(r, g, b));
    }
}

inline void buildIcon(unsigned char* out) {
  memset(out, 0, kIconFileSize);
  unsigned char* p = out;
  putU32(p, kIconMagic); putU32(p + 4, 1); putU32(p + 8, 7); putF32(p + 12, 1.0f); putU32(p + 16, kIconVertices);
  p += kIconHeaderSize;
  const short h = 3000;   // half edge: 0.73 of the unit cube
  struct Face { int n[3]; int u[3]; int v[3]; };   // normal axis, then the two tangent axes
  static const Face faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                                {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
  static const int quad[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};   // two triangles
  for (int f = 0; f < 6; ++f) {
    for (int i = 0; i < 6; ++i) {
      const int a = quad[i][0], b = quad[i][1];
      short pos[3];
      for (int k = 0; k < 3; ++k) pos[k] = static_cast<short>(h * faces[f].n[k] + h * a * faces[f].u[k] + h * b * faces[f].v[k]);
      for (int k = 0; k < 3; ++k) putU16(p + 2 * k, static_cast<unsigned>(static_cast<unsigned short>(pos[k])));
      for (int k = 0; k < 3; ++k) putU16(p + 8 + 2 * k, static_cast<unsigned>(static_cast<unsigned short>(static_cast<short>(faces[f].n[k] * 4096))));
      putU16(p + 16, static_cast<unsigned>((a + 1) * 2048));
      putU16(p + 18, static_cast<unsigned>((b + 1) * 2048));
      p[20] = p[21] = p[22] = p[23] = 0x80;
      p += kIconVertexSize;
    }
  }
  putU32(p, 1); putU32(p + 4, 1); putF32(p + 8, 1.0f); putU32(p + 12, 0); putU32(p + 16, 1);   // one animation, one frame
  putU32(p + 20, 1); putU32(p + 24, 2);                                                         // shape 1, two keys
  putF32(p + 28, 1.0f); putF32(p + 32, 1.0f); putF32(p + 36, 1.0f); putF32(p + 40, 1.0f);
  p += kIconAnimSize;
  buildIconTexture(p);
}

}  // namespace ps2icon
}  // namespace rensga
