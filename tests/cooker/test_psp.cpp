// F4.05 (PSP Engine 3): PSP texture cooking (formats, padding, swizzle), compact meshes (MSHX), the RAM ceiling.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga/cooker/psp_format.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

namespace {

fs::path pspFixture() { return fs::path(RENSGA_FIXTURES_DIR) / "assets2d_psp"; }

CookResult cookFor(const fs::path& project, const char* target, std::string* firstError = nullptr, bool force = true) {
  CookOptions o;
  o.project = project;
  o.target = target;
  o.force = force;
  return cookProject(o, [&](const CookMessage& m) {
    if (m.severity == CookMessage::Error) {
      fprintf(stderr, "    [cook error] %s\n", m.text.c_str());
      if (firstError && firstError->empty()) *firstError = m.text;
    }
  });
}

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Loaded {
  Arena arena;
  PackView view;
  std::vector<uint8_t> bytes;
  bool open(const fs::path& p) {
    bytes = readAll(p);
    arena.create(bytes.size() + 64);
    u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
    memcpy(mem, bytes.data(), bytes.size());
    return view.open(mem, bytes.size(), p.string().c_str());  // runs validateAssets
  }
};

Guid guidOf(int n) {
  char t[64];
  snprintf(t, sizeof t, "00000000-0000-4000-8000-0000000a2d%02x", n);
  Guid g;
  Guid::parse(t, g);
  return g;
}

ImageRgba solid(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) {
  ImageRgba img;
  img.width = w;
  img.height = h;
  img.rgba.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    img.rgba[i * 4] = r; img.rgba[i * 4 + 1] = g; img.rgba[i * 4 + 2] = b; img.rgba[i * 4 + 3] = a;
  }
  return img;
}

// Unswizzled stored pixels of a cooked texture.
std::vector<uint8_t> linearOf(const CookedTexture& t) {
  const u32 pitch = t.storedWidth * pspBitsPerPixel(t.format) / 8;
  std::vector<uint8_t> lin(t.pixels.size());
  pspUnswizzle(t.pixels.data(), pitch, t.storedHeight, lin.data());
  return lin;
}

// Minimal PNG writer (stored deflate blocks) for the oversize-texture cook test.
u32 crc32Of(const u8* p, size_t n, u32 c = 0) {
  c = ~c;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}
void writePng(const fs::path& path, u32 w, u32 h) {
  std::vector<u8> raw;
  for (u32 y = 0; y < h; ++y) {
    raw.push_back(0);
    for (u32 x = 0; x < w; ++x) { raw.push_back(10); raw.push_back(20); raw.push_back(30); raw.push_back(255); }
  }
  std::vector<u8> z = {0x78, 0x01};
  size_t pos = 0;
  u32 a = 1, b = 0;
  for (u8 v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  while (pos < raw.size()) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
    z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + n));
    pos += n;
  }
  const u32 ad = (b << 16) | a;
  for (int s = 24; s >= 0; s -= 8) z.push_back(static_cast<u8>(ad >> s));
  std::vector<u8> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  auto chunk = [&](const char* t, const std::vector<u8>& d) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<u8>(d.size() >> s));
    std::vector<u8> td(t, t + 4);
    td.insert(td.end(), d.begin(), d.end());
    out.insert(out.end(), td.begin(), td.end());
    const u32 c = crc32Of(td.data(), td.size());
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<u8>(c >> s));
  };
  std::vector<u8> ihdr;
  for (u32 v : {w, h}) for (int s = 24; s >= 0; s -= 8) ihdr.push_back(static_cast<u8>(v >> s));
  ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

}  // namespace

TEST(psp_swizzle_roundtrip) {
  const u32 pitch = 64, rows = 24;  // 4 block columns, 3 bands
  std::vector<uint8_t> src(pitch * rows), sw(src.size()), back(src.size());
  for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>(i * 31 + (i >> 8) * 7);
  pspSwizzle(src.data(), pitch, rows, sw.data());
  CHECK(sw != src);
  pspUnswizzle(sw.data(), pitch, rows, back.data());
  CHECK(back == src);
  // Block order: bands top to bottom, 16-byte columns left to right, rows inside a block top to bottom.
  const u32 x = 37, y = 19;  // block column 2, band 2, row 3 of the block, byte 5 of the row
  const size_t at = ((y / 8) * (pitch / 16) + (x / 16)) * 128 + (y % 8) * 16 + (x % 16);
  CHECK_EQ(sw[at], src[static_cast<size_t>(y) * pitch + x]);
}

TEST(psp_texture_padding_and_minimum_sizes) {
  CookedTexture t;
  std::string err;
  // 20x12 RGBA8 -> 32x16 stored, source size kept, swizzled, padding zero.
  ImageRgba img = solid(20, 12, 200, 100, 50, 255);
  CHECK(pspCookTexture(img, "rgba8", t, err));
  CHECK_EQ(t.width, 20u);
  CHECK_EQ(t.height, 12u);
  CHECK_EQ(t.storedWidth, 32u);
  CHECK_EQ(t.storedHeight, 16u);
  CHECK_EQ(t.flags, static_cast<u32>(kTexSwizzled));
  CHECK_EQ(t.format, static_cast<u32>(PackTexFormat::RGBA8));
  CHECK_EQ(t.pixels.size(), 32u * 16u * 4u);
  std::vector<uint8_t> lin = linearOf(t);
  bool padZero = true, srcOk = true;
  for (u32 y = 0; y < 16; ++y)
    for (u32 x = 0; x < 32; ++x)
      for (u32 c = 0; c < 4; ++c) {
        const u8 v = lin[(static_cast<size_t>(y) * 32 + x) * 4 + c];
        if (x < 20 && y < 12) srcOk = srcOk && v == img.rgba[(static_cast<size_t>(y) * 20 + x) * 4 + c];
        else padZero = padZero && v == 0;
      }
  CHECK(srcOk);
  CHECK(padZero);

  // Minimum stored widths: CLUT4 32, CLUT8 16, 16-bit and 32-bit 8; height 8.
  ImageRgba small = solid(3, 3, 1, 2, 3, 255);
  CHECK(pspCookTexture(small, "clut4", t, err));
  CHECK(t.storedWidth == 32 && t.storedHeight == 8 && t.pixels.size() == 32u * 8u / 2u && t.palette.size() == 16);
  CHECK(pspCookTexture(small, "clut8", t, err));
  CHECK(t.storedWidth == 16 && t.storedHeight == 8 && t.pixels.size() == 16u * 8u && t.palette.size() == 256);
  CHECK(pspCookTexture(small, "rgba5551", t, err));
  CHECK(t.storedWidth == 8 && t.storedHeight == 8 && t.pixels.size() == 8u * 8u * 2u);
  CHECK(pspCookTexture(small, "rgba4444", t, err));
  CHECK(t.storedWidth == 8 && t.storedHeight == 8 && t.pixels.size() == 8u * 8u * 2u);
  CHECK(pspCookTexture(small, "rgba8", t, err));
  CHECK(t.storedWidth == 8 && t.storedHeight == 8 && t.pixels.size() == 8u * 8u * 4u);
  // Already a power of two and big enough: no padding, still swizzled.
  CHECK(pspCookTexture(solid(64, 32, 1, 2, 3, 255), "clut4", t, err));
  CHECK(t.storedWidth == 64 && t.storedHeight == 32 && t.flags == kTexSwizzled);
}

TEST(psp_clut4_nibble_order_and_padding) {
  ImageRgba img;
  img.width = 5;  // odd width: the 5th pixel is alone in its byte
  img.height = 1;
  const u8 cols[5][4] = {{10, 0, 0, 255}, {20, 0, 0, 255}, {30, 0, 0, 255}, {40, 0, 0, 255}, {50, 0, 0, 255}};
  for (auto& c : cols) img.rgba.insert(img.rgba.end(), c, c + 4);
  CookedTexture t;
  std::string err;
  CHECK(pspCookTexture(img, "clut4", t, err));  // indices 0..4 in first-appearance order
  std::vector<uint8_t> lin = linearOf(t);
  CHECK_EQ(lin[0], 0x10);  // pixel 0 in the LOW nibble, pixel 1 in the high nibble
  CHECK_EQ(lin[1], 0x32);
  CHECK_EQ(lin[2], 0x04);
  for (size_t i = 3; i < lin.size(); ++i)
    if (lin[i] != 0) { CHECK(lin[i] == 0); break; }  // everything else is padding
  CHECK_EQ(t.palette.size(), 16u);
  CHECK_EQ(t.palette[4], 50u | (255u << 24));  // RGBA8 palette, R in the low byte
  // The default `auto` goes through the same palette code as the PS2 plugin.
  CookedTexture ps2 = makeClut(img, 0), a;
  CHECK(pspCookTexture(img, "auto", a, err));
  CHECK(a.palette == ps2.palette && a.format == ps2.format);
}

TEST(psp_16bit_channel_layout) {
  ImageRgba img;
  img.width = 4;
  img.height = 1;
  const u8 px[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 0}};
  for (auto& c : px) img.rgba.insert(img.rgba.end(), c, c + 4);
  CookedTexture t;
  std::string err;
  CHECK(pspCookTexture(img, "rgba5551", t, err));
  CHECK_EQ(t.format, static_cast<u32>(PackTexFormat::RGBA5551));
  std::vector<uint8_t> lin = linearOf(t);
  auto at = [&](int i) { return static_cast<u32>(lin[i * 2] | (lin[i * 2 + 1] << 8)); };
  CHECK_EQ(at(0), 0x801Fu);  // R bits 0-4, A bit 15
  CHECK_EQ(at(1), 0x83E0u);  // G bits 5-9
  CHECK_EQ(at(2), 0xFC00u);  // B bits 10-14
  CHECK_EQ(at(3), 0x7FFFu);  // alpha 0 clears bit 15
  CHECK(pspCookTexture(img, "rgba4444", t, err));
  CHECK_EQ(t.format, static_cast<u32>(PackTexFormat::RGBA4444));
  lin = linearOf(t);
  CHECK_EQ(at(0), 0xF00Fu);  // R bits 0-3, A bits 12-15
  CHECK_EQ(at(1), 0xF0F0u);  // G bits 4-7
  CHECK_EQ(at(2), 0xFF00u);  // B bits 8-11
  CHECK_EQ(at(3), 0x0FFFu);
  CHECK(!pspCookTexture(img, "rgb565", t, err));
  CHECK(err.find("rgb565") != std::string::npos && err.find("rgba5551") != std::string::npos);
}

TEST(psp_oversize_texture_error_suggests_max_size) {
  CookedTexture t;
  std::string err;
  CHECK(!pspCookTexture(solid(600, 20, 1, 2, 3, 255), "rgba8", t, err));
  CHECK(err.find("600x20") != std::string::npos);
  CHECK(err.find("512") != std::string::npos);
  CHECK(err.find("maxSize") != std::string::npos);
  CHECK(!pspCookTexture(solid(513, 8, 1, 2, 3, 255), "auto", t, err));
  CHECK(!pspCookTexture(solid(8, 513, 1, 2, 3, 255), "auto", t, err));
  CHECK(pspCookTexture(solid(512, 512, 1, 2, 3, 255), "clut4", t, err));  // the largest texture
  CHECK_EQ(t.storedWidth, 512u);
}

TEST(psp_cook_oversize_names_asset) {
  fs::path tmp = fs::temp_directory_path() / "rensga_psp_big";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  fs::copy(pspFixture() / "ProjectSettings", tmp / "ProjectSettings", fs::copy_options::recursive);
  fs::copy(pspFixture() / "Assets", tmp / "Assets", fs::copy_options::recursive);
  writePng(tmp / "Assets/Tiles/noisy.png", 600, 20);  // replaces the 32x32 one
  std::string err;
  CookResult r = cookFor(tmp, "psp", &err);
  CHECK(!r.ok);
  CHECK(err.find("noisy.png") != std::string::npos);
  CHECK(err.find("00000000-0000-4000-8000-0000000a2d02") != std::string::npos);
  CHECK(err.find("maxSize") != std::string::npos);
  // The suggested fix works: with maxSize 512 the same file cooks.
  {
    std::ofstream o(tmp / "Assets/Tiles/noisy.png.meta");
    o << R"({"guid":"00000000-0000-4000-8000-0000000a2d02","importSettings":{"maxSize":512}})";
  }
  CookResult r2 = cookFor(tmp, "psp");
  CHECK(r2.ok);
  fs::remove_all(tmp);
}

TEST(psp_cook_pack_formats_and_pixels) {
  CookResult psp = cookFor(pspFixture(), "psp");
  CookResult lin = cookFor(pspFixture(), "linux");
  CHECK(psp.ok && lin.ok);
  Loaded pp, lp;
  CHECK(pp.open(psp.packPath));  // PackView::open runs validateAssets
  CHECK(lp.open(lin.packPath));
  PackAssets a = pp.view.assets(), l = lp.view.assets();
  CHECK_EQ(a.textureCount, 5u);  // default font, noisy, tileset, odd, icon
  CHECK(pp.view.find(pack_section::MSHX) == nullptr);  // no meshes in this project

  struct Want { int guid; u32 format, w, h, sw, sh, palette; };
  const Want want[] = {
      {1, 1, 64, 32, 64, 32, 16},   // tileset: 5 colors -> CLUT4, already a power of two
      {2, 2, 32, 32, 32, 32, 256},  // noisy: ~1000 colors -> CLUT8
      {8, 1, 20, 12, 32, 16, 16},   // odd.png: 4 colors -> CLUT4, padded 20x12 -> 32x16
      {9, 3, 10, 10, 16, 16, 0},    // icon.png: importSettings rgba5551, padded 10x10 -> 16x16
  };
  for (const Want& w : want) {
    const PackTexRec* t = a.findTexture(guidOf(w.guid));
    CHECK(t != nullptr);
    if (!t) continue;
    CHECK_EQ(t->format, w.format);
    CHECK_EQ(t->width, w.w);   // source size
    CHECK_EQ(t->height, w.h);
    CHECK_EQ(t->storedW(), w.sw);
    CHECK_EQ(t->storedH(), w.sh);
    CHECK_EQ(static_cast<u32>(t->paletteCount), w.palette);
    CHECK_EQ(static_cast<u32>(t->flags), static_cast<u32>(kTexSwizzled));
    CHECK_EQ(t->pixelOffset % 16u, 0u);
    CHECK_EQ(t->paletteOffset % 16u, 0u);
    CHECK_EQ(t->pixelBytes, w.sw * w.sh * pspBitsPerPixel(w.format) / 8);

    // Decode the stored pixels and compare with the RGBA8 the linux cook of the same source holds.
    const PackTexRec* ref = l.findTexture(guidOf(w.guid));
    CHECK(ref != nullptr);
    if (!ref || w.format > 2) continue;  // 16-bit formats lose bits; their layout is covered above
    std::vector<uint8_t> lin2(t->pixelBytes);
    pspUnswizzle(a.texturePixels(*t), w.sw * pspBitsPerPixel(w.format) / 8, w.sh, lin2.data());
    const u32* pal = a.texturePalette(*t);
    const u8* refPx = l.texturePixels(*ref);
    bool same = true;
    for (u32 y = 0; y < w.h; ++y)
      for (u32 x = 0; x < w.w; ++x) {
        u32 idx = w.format == 1 ? ((lin2[y * (w.sw / 2) + x / 2] >> ((x & 1) * 4)) & 15) : lin2[y * w.sw + x];
        const u8* r = refPx + (static_cast<size_t>(y) * w.w + x) * 4;
        const u32 want32 = r[0] | (r[1] << 8) | (r[2] << 16) | (static_cast<u32>(r[3]) << 24);
        if (w.guid == 2) continue;  // noisy is palette-reduced; only the layout is checked
        same = same && pal[idx] == want32;
      }
    CHECK(same);
  }
  // Tilemap and clips read the SOURCE size of their texture, not the padded one.
  CHECK_EQ(a.tilemapCount, 1u);
  CHECK_EQ(a.animCount, 1u);
  // GL and GS refuse flags != 0, so a PSP project cooked for linux must stay unswizzled RGBA8.
  const PackTexRec* lt = l.findTexture(guidOf(8));
  CHECK(lt && lt->format == 0 && lt->flags == 0 && lt->storedW() == 20);
}

TEST(psp_cook_is_deterministic_and_cached) {
  CookResult a = cookFor(pspFixture(), "psp");
  std::vector<uint8_t> first = readAll(a.packPath);
  CookResult b = cookFor(pspFixture(), "psp", nullptr, false);  // now served from Library/Cache
  CookResult c = cookFor(pspFixture(), "psp");
  CHECK(a.ok && b.ok && c.ok);
  CHECK(first == readAll(b.packPath));
  CHECK(first == readAll(c.packPath));
}

TEST(psp_compact_mesh_precision) {
  std::vector<PackVertex> v;
  unsigned seed = 12345;
  auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (static_cast<float>(seed >> 8) / 16777216.0f) - 0.5f; };
  for (int i = 0; i < 600; ++i) {
    PackVertex p;
    p.color = 0xFF000000u | static_cast<u32>(i * 977);
    p.x = 40.0f + rnd() * 200.0f;   // 40 +- 100
    p.y = rnd() * 6.0f;
    p.z = -3.0f + rnd() * 0.5f;
    v.push_back(p);
  }
  std::vector<uint8_t> in(reinterpret_cast<uint8_t*>(v.data()), reinterpret_cast<uint8_t*>(v.data()) + v.size() * sizeof(PackVertex));
  std::vector<uint8_t> out;
  PackMeshExt ext = {};
  std::string err;
  CHECK(pspCompactVertices(in, out, ext, err));
  CHECK_EQ(ext.format, kMeshFormatCompact16);
  CHECK_EQ(out.size(), v.size() * sizeof(PackVertexCompact));
  const PackVertexCompact* c = reinterpret_cast<const PackVertexCompact*>(out.data());
  for (int k = 0; k < 3; ++k) {
    int e;
    CHECK_EQ(frexpf(ext.scale[k], &e), 0.5f);  // a power of two
  }
  float worst = 0;
  bool colorsOk = true;
  for (size_t i = 0; i < v.size(); ++i) {
    const float r[3] = {ext.offset[0] + c[i].x * ext.scale[0], ext.offset[1] + c[i].y * ext.scale[1], ext.offset[2] + c[i].z * ext.scale[2]};
    const float o[3] = {v[i].x, v[i].y, v[i].z};
    for (int k = 0; k < 3; ++k) {
      CHECK(fabsf(r[k] - o[k]) <= ext.scale[k] * 0.5f + 1e-4f);
      worst = fmaxf(worst, fabsf(r[k] - o[k]));
    }
    colorsOk = colorsOk && c[i].color == v[i].color && c[i].pad == 0;
  }
  CHECK(colorsOk);
  CHECK(worst < 0.004f);  // a 200-unit span is stored with better than 4 mm resolution
  // The unit cube used by the templates survives exactly.
  std::vector<PackVertex> cube;
  for (int i = 0; i < 8; ++i) cube.push_back({0xFFFFFFFFu, (i & 1) - 0.5f, ((i >> 1) & 1) - 0.5f, ((i >> 2) & 1) - 0.5f});
  std::vector<uint8_t> cin(reinterpret_cast<uint8_t*>(cube.data()), reinterpret_cast<uint8_t*>(cube.data()) + cube.size() * sizeof(PackVertex));
  CHECK(pspCompactVertices(cin, out, ext, err));
  const PackVertexCompact* cc = reinterpret_cast<const PackVertexCompact*>(out.data());
  bool exact = true;
  for (int i = 0; i < 8; ++i)
    exact = exact && ext.offset[0] + cc[i].x * ext.scale[0] == cube[i].x && ext.offset[1] + cc[i].y * ext.scale[1] == cube[i].y &&
            ext.offset[2] + cc[i].z * ext.scale[2] == cube[i].z;
  CHECK(exact);
  // Nothing to compact, or a torn buffer.
  CHECK(!pspCompactVertices(std::vector<uint8_t>(), out, ext, err) && err.empty());
  CHECK(!pspCompactVertices(std::vector<uint8_t>(10, 0), out, ext, err) && !err.empty());
}

TEST(psp_mesh_cook_compact_only_for_psp_target) {
  // Copy only the sources of a primitive-only psp project so parallel tests never share a Library folder.
  fs::path tmp = fs::temp_directory_path() / "rensga_psp_mesh";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok/ProjectSettings", tmp / "ProjectSettings", fs::copy_options::recursive);
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok/Assets", tmp / "Assets", fs::copy_options::recursive);
  CookResult psp = cookFor(tmp, "psp");
  CookResult lin = cookFor(tmp, "linux");
  CHECK(psp.ok && lin.ok);
  Loaded pp, lp;
  CHECK(pp.open(psp.packPath));
  CHECK(lp.open(lin.packPath));
  const PackSection* pv = pp.view.find(pack_section::VERT);
  const PackSection* lv = lp.view.find(pack_section::VERT);
  const PackSection* px = pp.view.find(pack_section::MSHX);
  CHECK(pv && lv && px);
  CHECK(lp.view.find(pack_section::MSHX) == nullptr);  // a linux cook of a PSP project keeps float vertices
  if (!pv || !lv || !px) { fs::remove_all(tmp); return; }
  CHECK_EQ(pv->count, lv->count);
  CHECK(pv->count > 0);
  CHECK_EQ(pv->size, pv->count * static_cast<u32>(sizeof(PackVertexCompact)));
  CHECK_EQ(lv->size, lv->count * static_cast<u32>(sizeof(PackVertex)));
  CHECK_EQ(px->size, static_cast<u32>(sizeof(PackMeshExt)));
  CHECK_EQ(pp.view.find(pack_section::MESH)->count, lp.view.find(pack_section::MESH)->count);
  const PackMeshExt* ext = pp.view.records<PackMeshExt>(pack_section::MSHX);
  const PackVertexCompact* cv = pp.view.records<PackVertexCompact>(pack_section::VERT);
  const PackVertex* fv = lp.view.records<PackVertex>(pack_section::VERT);
  bool close = true;
  for (u32 i = 0; i < pv->count; ++i) {
    close = close && cv[i].color == fv[i].color && fabsf(ext->offset[0] + cv[i].x * ext->scale[0] - fv[i].x) <= ext->scale[0] * 0.5f + 1e-4f &&
            fabsf(ext->offset[1] + cv[i].y * ext->scale[1] - fv[i].y) <= ext->scale[1] * 0.5f + 1e-4f &&
            fabsf(ext->offset[2] + cv[i].z * ext->scale[2] - fv[i].z) <= ext->scale[2] * 0.5f + 1e-4f;
  }
  CHECK(close);
  fs::remove_all(tmp);
}

TEST(psp_ram_budget_ceiling) {
  ConsoleTargetData t;
  t.profile = "psp";
  t.ramLimitBytes = profileRamLimitBytes("psp");
  CHECK_EQ(t.ramLimitBytes, 32ull * 1024 * 1024);
  CHECK_EQ(profileRamLimitBytes("ps2"), 0ull);
  t.ramBytes = 24ull * 1024 * 1024;
  SceneBudgetInput in;
  in.sceneAsset = "Assets/Scenes/Main.scene";
  in.sceneGuid = "00000000-0000-4000-8000-000000000001";
  std::vector<CookMessage> errors;
  CHECK(validateBudgets(t, in, errors));
  t.ramBytes = 40ull * 1024 * 1024;
  CHECK(!validateBudgets(t, in, errors));
  CHECK(errors.size() == 1 && errors[0].text.find("psp device limit") != std::string::npos);
  // The loader fills the ceiling from the profile.
  ConsoleTargetData loaded = loadConsoleTarget(pspFixture() / "ProjectSettings" / "ConsoleTarget.json");
  CHECK_EQ(loaded.ramLimitBytes, 32ull * 1024 * 1024);
  CHECK(loaded.fbBpp == 16 && loaded.fbBuffers == 2);
  CHECK_EQ(loaded.textureFormats.size(), 5u);
}
