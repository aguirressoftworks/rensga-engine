// F4.01: byte layout of the PS2 memory card icon (icon.sys + the 3D icon), checked on the host. The same header is compiled into the
// PS2 platform backend, which writes both files to mc0:/RENSGA the first time the card is opened.
#include "rensga/platform/ps2_icon.h"
#include "rensga_test.h"

using namespace rensga::ps2icon;

namespace {
unsigned rd16(const unsigned char* p) { return p[0] | (p[1] << 8); }
unsigned rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<unsigned>(p[3]) << 24); }
float rdF(const unsigned char* p) { unsigned v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
}  // namespace

TEST(ps2_icon_sys_has_the_documented_layout) {
  static_assert(kIconSysSize == 964, "icon.sys is 964 bytes");
  unsigned char sys[kIconSysSize + 1];
  memset(sys, 0xEE, sizeof sys);
  CHECK(buildIconSys(sys, "Rensga Engine", " Platformer 2D", "rensga.ico"));
  CHECK_EQ(sys[kIconSysSize], 0xEE);   // nothing written past the end
  CHECK_EQ(memcmp(sys, "PS2D", 4), 0);
  CHECK_EQ(rd16(sys + 4), 0u);
  CHECK_EQ(rd16(sys + 6), 13u);        // the second title line starts after "Rensga Engine"
  CHECK_EQ(rd32(sys + 8), 0u);
  CHECK_EQ(rd32(sys + 0x0C), 64u);     // background transparency (0..128)
  for (int i = 0; i < 4; ++i) {        // 4 corner colours: R, G, B in 0..255, the fourth i32 is 0
    for (int c = 0; c < 3; ++c) CHECK(rd32(sys + 0x10 + 16 * i + 4 * c) <= 255u);
    CHECK_EQ(rd32(sys + 0x10 + 16 * i + 12), 0u);
  }
  for (int i = 0; i < 3; ++i) {        // light directions are unit-ish, colours 0..1, the 4th float is 0
    CHECK_EQ(rd32(sys + 0x50 + 16 * i + 12), 0u);
    CHECK_EQ(rd32(sys + 0x80 + 16 * i + 12), 0u);
    for (int c = 0; c < 3; ++c) {
      const float col = rdF(sys + 0x80 + 16 * i + 4 * c);
      CHECK(col >= 0.0f && col <= 1.0f);
    }
  }
  CHECK_NEAR(rdF(sys + 0xB0), 0.4f, 1e-6);
  CHECK_EQ(memcmp(sys + kOffTitle, "Rensga Engine Platformer 2D", 27), 0);
  CHECK_EQ(sys[kOffTitle + 27], 0);
  CHECK_EQ(strcmp(reinterpret_cast<const char*>(sys + kOffView), "rensga.ico"), 0);
  CHECK_EQ(strcmp(reinterpret_cast<const char*>(sys + kOffCopy), "rensga.ico"), 0);
  CHECK_EQ(strcmp(reinterpret_cast<const char*>(sys + kOffDelete), "rensga.ico"), 0);
  for (unsigned i = 0x1C4; i < kIconSysSize; ++i) CHECK_EQ(sys[i], 0);   // reserved tail
  // Offsets of the documented fields.
  CHECK_EQ(kOffTitle, 0xC0u);
  CHECK_EQ(kOffView, 0x104u);
  CHECK_EQ(kOffCopy, 0x144u);
  CHECK_EQ(kOffDelete, 0x184u);
  // Too long: refused instead of overflowing the 68-byte title or the 64-byte file names.
  CHECK(!buildIconSys(sys, "0123456789012345678901234567890123", "0123456789012345678901234567890123", "a.ico"));
  CHECK(!buildIconSys(sys, "A", "B", "0123456789012345678901234567890123456789012345678901234567890123456789"));
}

TEST(ps2_icon_file_has_the_documented_layout) {
  static unsigned char icon[kIconFileSize + 1];
  memset(icon, 0xEE, sizeof icon);
  buildIcon(icon);
  CHECK_EQ(icon[kIconFileSize], 0xEE);
  CHECK_EQ(rd32(icon), 0x00010000u);   // magic
  CHECK_EQ(rd32(icon + 4), 1u);        // animation shapes
  CHECK_EQ(rd32(icon + 8), 7u);        // uncompressed texture
  CHECK_NEAR(rdF(icon + 12), 1.0f, 1e-6);
  CHECK_EQ(rd32(icon + 16), 36u);      // 12 triangles, a multiple of 3
  CHECK_EQ(kIconFileSize, 20u + 36 * 24 + 44 + 128 * 128 * 2);
  const unsigned char* v = icon + kIconHeaderSize;
  for (unsigned i = 0; i < kIconVertices; ++i, v += kIconVertexSize) {
    // position components are +-3000 or on a face between them (|x| <= 3 * 3000 never happens: one axis is the face, two span it)
    int axesOnFace = 0;
    for (int k = 0; k < 3; ++k) {
      const int c = static_cast<short>(rd16(v + 2 * k));
      CHECK(c == 3000 || c == -3000);
      (void)axesOnFace;
    }
    CHECK_EQ(rd16(v + 6), 0u);           // w
    const int nx = static_cast<short>(rd16(v + 8)), ny = static_cast<short>(rd16(v + 10)), nz = static_cast<short>(rd16(v + 12));
    CHECK_EQ((nx != 0) + (ny != 0) + (nz != 0), 1);                 // an axis-aligned normal of length 1.0 (4096)
    CHECK(nx == 4096 || nx == -4096 || nx == 0);
    CHECK(rd16(v + 16) <= 4096u && rd16(v + 18) <= 4096u);          // uv in 0..1
    CHECK_EQ(v[20], 0x80);
    CHECK_EQ(v[23], 0x80);
  }
  const unsigned char* anim = icon + kIconHeaderSize + kIconVertices * kIconVertexSize;
  CHECK_EQ(rd32(anim), 1u);            // animation id
  CHECK_EQ(rd32(anim + 16), 1u);       // one frame
  CHECK_EQ(rd32(anim + 20), 1u);       // shape 1
  CHECK_EQ(rd32(anim + 24), 2u);       // two keys
  const unsigned char* tex = anim + kIconAnimSize;
  CHECK_EQ(tex - icon + 128 * 128 * 2, kIconFileSize);
  unsigned opaque = 0, dark = 0;
  for (unsigned i = 0; i < 128 * 128; ++i) {
    const unsigned px = rd16(tex + 2 * i);
    if (px & 0x8000) ++opaque;
    if ((px & 0x1F) < 8) ++dark;       // the letter is dark: red below 8/32
  }
  CHECK_EQ(opaque, 128u * 128u);       // fully opaque
  CHECK(dark > 500 && dark < 8000);    // the letter R is there
}
