// F2.03: asset pipeline tests. Cooks tests/fixtures/assets2d for the linux and ps2 targets and reads
// the packs back through PackView.
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga/core/pack_watcher.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

namespace {

CookResult cookAs(const fs::path& project, const char* target, std::string* firstError = nullptr) {
  CookOptions o;
  o.project = project;
  o.target = target;
  o.force = true;
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
    return view.open(mem, bytes.size(), p.string().c_str());
  }
};

fs::path fixture() { return fs::path(RENSGA_FIXTURES_DIR) / "assets2d"; }

Guid guidOf(int n) {
  char t[64];
  snprintf(t, sizeof t, "00000000-0000-4000-8000-0000000a2d%02x", n);
  Guid g;
  Guid::parse(t, g);
  return g;
}

}  // namespace

TEST(assets_cook_and_load_ps2) {
  CookResult r = cookAs(fixture(), "ps2");
  CHECK(r.ok);
  Loaded pk;
  CHECK(pk.open(r.packPath));
  CHECK_EQ(pk.view.header->version, 2u);
  // media "iso": sections start on 2048-byte sectors.
  CHECK((pk.view.header->flags & kPackFlagSectorAligned) != 0);
  CHECK_EQ(pk.view.header->alignment, 2048u);
  for (u32 i = 0; i < pk.view.header->sectionCount; ++i) CHECK_EQ(pk.view.sections[i].offset % 2048u, 0u);

  PackAssets a = pk.view.assets();
  CHECK_EQ(a.textureCount, 3u);  // default font, noisy, tileset (project-path order)
  const PackTexRec* tiles = a.findTexture(guidOf(1));
  const PackTexRec* noisy = a.findTexture(guidOf(2));
  CHECK(tiles && noisy);
  if (!tiles || !noisy) return;
  // 5 unique colors: CLUT4 with a 16-entry palette; nibbles pack two pixels per byte.
  CHECK_EQ(tiles->format, static_cast<u32>(PackTexFormat::CLUT4));
  CHECK_EQ(tiles->paletteCount, 16u);
  CHECK_EQ(tiles->pixelBytes, 64u * 32u / 2u);
  const u32* pal = a.texturePalette(*tiles);
  CHECK(pal != nullptr);
  const u8* px = a.texturePixels(*tiles);
  // Pixel (0,0) is tile 0's border: cols[4] = (230,200,40,255); index 0 in first-appearance order.
  u32 c0 = pal[px[0] & 15];
  CHECK_EQ(c0, 230u | (200u << 8) | (40u << 16) | (255u << 24));
  // ~1000 unique colors: CLUT8, 256-entry palette from the deterministic median cut.
  CHECK_EQ(noisy->format, static_cast<u32>(PackTexFormat::CLUT8));
  CHECK_EQ(noisy->paletteCount, 256u);
  CHECK_EQ(noisy->pixelBytes, 32u * 32u);
  // The default font's texture has 2 colors, so it is CLUT4 too.
  CHECK_EQ(a.textures[0].format, static_cast<u32>(PackTexFormat::CLUT4));

  CHECK_EQ(a.tilemapCount, 1u);
  const PackTilemapRec* map = a.findTilemap(guidOf(4));
  CHECK(map != nullptr);
  if (map) {
    CHECK_EQ(map->width, 4u);
    CHECK_EQ(map->height, 3u);
    CHECK_EQ(map->tileSize, 16u);
    CHECK_EQ(&a.textures[map->tilesetTexture], tiles);
    const u16* t = a.tilemapTiles(*map);
    CHECK_EQ(t[1], 1u);
    CHECK_EQ(t[7], 7u);
    CHECK_EQ(t[10], 65535u);
    CHECK_EQ(map->solidCount, 8u);
    const u8* solid = a.tilemapSolid(*map);
    CHECK(solid[1] == 1 && solid[5] == 1 && solid[0] == 0 && solid[2] == 0);
  }

  CHECK_EQ(a.animCount, 1u);
  const PackAnimRec* an = a.findAnim(guidOf(5));
  CHECK(an != nullptr);
  if (an) {
    CHECK_EQ(an->frameCount, 4u);
    CHECK_EQ(an->fps, 8u);
    CHECK_EQ(an->loop, 1u);
    CHECK(strcmp(pk.view.string(an->nameStr), "walk") == 0);
    const PackFrameRect* f = a.animFrames(*an);
    CHECK(f[1].x == 16 && f[1].y == 0 && f[1].w == 16 && f[1].h == 16);
    CHECK(f[3].x == 48 && f[3].y == 0);
  }

  CHECK_EQ(a.fontCount, 2u);  // default font + mono.font.json
  const PackFontRec* mono = a.findFont(guidOf(6));
  CHECK(mono && mono->charCount == 8 && mono->advance[7] == 12 && mono->firstChar == 65);
  Guid defGuid;
  Guid::parse(kDefaultFontGuid, defGuid);
  const PackFontRec* def = a.findFont(defGuid);
  CHECK(def && def->glyphW == 8 && def->charCount == 95 && def->texture == 0);

  // smallerAudio on PS2: stereo 44100 Hz x 200 frames becomes mono 22050 Hz x 100 frames.
  CHECK_EQ(a.audioCount, 1u);
  const PackAudioRec* au = a.findAudio(guidOf(3));
  CHECK(au != nullptr);
  if (au) {
    CHECK_EQ(au->channels, 1u);
    CHECK_EQ(au->sampleRate, 22050u);
    CHECK_EQ(au->frameCount, 100u);
    CHECK_EQ(au->loops, 1u);
    CHECK_EQ(au->loopStart, 10u);
    CHECK_EQ(au->loopEnd, 90u);
  }
}

TEST(assets_cook_and_load_linux) {
  CookResult r = cookAs(fixture(), "linux");
  CHECK(r.ok);
  Loaded pk;
  CHECK(pk.open(r.packPath));
  PackAssets a = pk.view.assets();
  const PackTexRec* tiles = a.findTexture(guidOf(1));
  const PackTexRec* noisy = a.findTexture(guidOf(2));
  CHECK(tiles && noisy);
  if (!tiles || !noisy) return;
  CHECK_EQ(tiles->format, static_cast<u32>(PackTexFormat::RGBA8));
  CHECK_EQ(tiles->paletteCount, 0u);
  CHECK_EQ(tiles->pixelBytes, 64u * 32u * 4u);
  CHECK_EQ(noisy->format, static_cast<u32>(PackTexFormat::RGBA8));
  CHECK_EQ(noisy->pixelBytes, 32u * 32u * 4u);
  // The PNG's first pixel survives unchanged.
  const u8* px = a.texturePixels(*tiles);
  CHECK(px[0] == 230 && px[1] == 200 && px[2] == 40 && px[3] == 255);

  const PackAudioRec* au = a.findAudio(guidOf(3));
  CHECK(au != nullptr);
  if (au) {  // "smallerAudio" is a PS2 setting: PC keeps the stereo 44100 Hz source.
    CHECK_EQ(au->channels, 2u);
    CHECK_EQ(au->sampleRate, 44100u);
    CHECK_EQ(au->frameCount, 200u);
    CHECK_EQ(au->loopStart, 20u);
    CHECK_EQ(au->loopEnd, 180u);
    const i16* s = a.audioSamples(*au);
    CHECK_EQ(s[0], -10000);
    CHECK_EQ(s[1], 10000);
  }
  CHECK_EQ(a.tilemapCount, 1u);
  CHECK_EQ(a.animCount, 1u);
}

TEST(assets_cook_is_deterministic) {
  CookResult a = cookAs(fixture(), "ps2");
  std::vector<uint8_t> first = readAll(a.packPath);
  CookResult b = cookAs(fixture(), "ps2");
  CHECK(a.ok && b.ok);
  CHECK(first == readAll(b.packPath));
  CHECK(!first.empty());
}

TEST(assets_median_cut_is_deterministic) {
  ImageRgba img;
  img.width = img.height = 40;
  img.rgba.resize(40 * 40 * 4);
  for (u32 i = 0; i < 40 * 40; ++i) {
    img.rgba[i * 4 + 0] = static_cast<u8>(i * 7);
    img.rgba[i * 4 + 1] = static_cast<u8>(i * 13 + 5);
    img.rgba[i * 4 + 2] = static_cast<u8>(i * 3);
    img.rgba[i * 4 + 3] = 255;
  }
  CookedTexture x = makeClut(img, 0), y = makeClut(img, 0), z = makeClut(img, 4);
  CHECK_EQ(x.format, 2u);
  CHECK_EQ(x.palette.size(), 256u);
  CHECK(x.pixels == y.pixels && x.palette == y.palette);
  CHECK_EQ(z.format, 1u);
  CHECK_EQ(z.palette.size(), 16u);
  CHECK_EQ(z.pixels.size(), 20u * 40u);
}

TEST(assets_wav_pcm8_and_pcm16) {
  // 8-bit mono, 4 frames: 128 -> 0, 255 -> 127*256, 0 -> -128*256.
  std::vector<uint8_t> w = {'R', 'I', 'F', 'F', 40, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                            0x22, 0x56, 0, 0, 0x22, 0x56, 0, 0, 1, 0, 8, 0, 'd', 'a', 't', 'a', 4, 0, 0, 0, 128, 255, 0, 128};
  PcmAudio pcm;
  std::string err;
  CHECK(parseWav(w, pcm, err));
  CHECK_EQ(pcm.sampleRate, 22050u);
  CHECK_EQ(pcm.frames(), 4u);
  CHECK_EQ(pcm.samples[0], 0);
  CHECK_EQ(pcm.samples[1], 127 * 256);
  CHECK_EQ(pcm.samples[2], -128 * 256);
  audioHalveRate(pcm);
  CHECK_EQ(pcm.frames(), 2u);
  CHECK_EQ(pcm.sampleRate, 11025u);
  std::vector<uint8_t> bad = {'R', 'I', 'F', 'X'};
  CHECK(!parseWav(bad, pcm, err));
}

TEST(assets_gltf_triangle) {
  fs::path dir = fs::temp_directory_path() / "rensga_gltf_test";
  fs::create_directories(dir);
  fs::path f = dir / "tri.gltf";
  std::ofstream(f) << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
    "nodes":[{"mesh":0,"translation":[0,2,0]}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
    "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}}],
    "buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}],
    "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],
    "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}]})";
  PrimitiveMesh m;
  std::string err;
  CHECK(importGltf(f, m, err));
  CHECK_EQ(m.positions.size(), 3u);
  if (m.positions.size() == 3) {
    CHECK_NEAR(m.positions[1].x, 1.0f, 1e-5f);
    CHECK_NEAR(m.positions[2].y, 3.0f, 1e-5f);  // node translation applied
    CHECK_NEAR(m.normals[0].z, 1.0f, 1e-5f);    // flat normal of the CCW triangle
    CHECK(m.colors[0].r == 255 && m.colors[0].g == 0);
  }
  fs::remove_all(dir);
}

TEST(assets_bad_tilemap_names_asset) {
  fs::path tmp = fs::temp_directory_path() / "rensga_assets2d_bad";
  fs::remove_all(tmp);
  fs::copy(fixture(), tmp, fs::copy_options::recursive);
  fs::remove_all(tmp / "Library");
  {
    std::ofstream o(tmp / "Assets/Tiles/level.tilemap.json");
    o << R"({"format":1,"tileset":"00000000-0000-4000-8000-0000000a2dff","tileSize":16,"width":1,"height":1,"tiles":[0]})";
  }
  std::string err;
  CookResult r = cookAs(tmp, "ps2", &err);
  CHECK(!r.ok);
  CHECK(err.find("level.tilemap.json") != std::string::npos);
  CHECK(err.find("00000000-0000-4000-8000-0000000a2d04") != std::string::npos);
  CHECK(err.find("00000000-0000-4000-8000-0000000a2dff") != std::string::npos);
  fs::remove_all(tmp);
}

TEST(assets_v1_and_v2_packs_load) {
  CookResult r = cookAs(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok", "psp");
  CHECK(r.ok);
  Loaded v2;
  CHECK(v2.open(r.packPath));
  CHECK_EQ(v2.view.header->version, 2u);
  CHECK(v2.view.assets().textureCount >= 1);
  // A v1 pack is the same layout with the version field set to 1 (no tag, no asset sections read).
  std::vector<uint8_t> bytes = readAll(r.packPath);
  u32 one = 1;
  memcpy(bytes.data() + 4, &one, 4);
  fs::path v1path = fs::temp_directory_path() / "rensga_v1.rpak";
  std::ofstream(v1path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  Loaded v1;
  CHECK(v1.open(v1path));
  CHECK_EQ(v1.view.header->version, 1u);
  u32 bad = 3;
  memcpy(bytes.data() + 4, &bad, 4);
  std::ofstream(v1path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  Loaded v3;
  CHECK(!v3.open(v1path));
  fs::remove(v1path);
}

TEST(assets_pack_watcher_sees_rewrite) {
  fs::path f = fs::temp_directory_path() / "rensga_watch.rpak";
  std::ofstream(f, std::ios::binary) << "one";
  PackWatcher w;
  w.watch(f.string().c_str());
  CHECK(!w.check());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::ofstream(f, std::ios::binary) << "three";  // size changes, so the stamp changes even on a coarse clock
  CHECK(w.check());
  CHECK(!w.check());
  fs::remove(f);
  CHECK(!w.check());  // a vanished file is not a reload
  std::ofstream(f, std::ios::binary) << "back!";
  CHECK(w.check());
  fs::remove(f);
}

// F2.03 per-asset cache: a temp copy of the project is cooked repeatedly; counts come from CookResult.
TEST(assets_cook_cache_reuses_unchanged_assets) {
  const fs::path tmp = fs::temp_directory_path() / "rensga_cache_test";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  for (const auto& e : fs::recursive_directory_iterator(fixture())) {
    const fs::path rel = fs::relative(e.path(), fixture());
    if (*rel.begin() == "Library") continue;
    if (e.is_directory()) fs::create_directories(tmp / rel);
    else fs::copy_file(e.path(), tmp / rel, fs::copy_options::overwrite_existing);
  }
  auto cook = [&](const char* target, bool force) {
    CookOptions o;
    o.project = tmp;
    o.target = target;
    o.force = force;
    return cookProject(o);
  };
  for (const char* target : {"ps2", "linux"}) {
    CookResult first = cook(target, false);
    CHECK(first.ok && !first.upToDate);
    CHECK_EQ(first.assetsCooked, 3u);  // noisy.png, tileset.png, sfx.wav
    CHECK_EQ(first.assetsCached, 0u);
    // Nothing changed: the whole-project skip applies, so no asset is cooked.
    CookResult second = cook(target, false);
    CHECK(second.ok && second.upToDate);
    CHECK_EQ(second.assetsCooked, 0u);
    // A project file changes but no asset does: the pack is rebuilt, every asset comes from the cache.
    {
      std::ofstream(tmp / "ProjectSettings" / "InputActions.json", std::ios::binary | std::ios::app) << "\n";
    }
    CookResult third = cook(target, false);
    CHECK(third.ok && !third.upToDate);
    CHECK_EQ(third.assetsCooked, 0u);
    CHECK_EQ(third.assetsCached, 3u);
    const std::vector<uint8_t> cachedPack = readAll(third.packPath);
    // One PNG changes (bytes appended after IEND, which the decoder ignores): exactly that one is cooked again.
    {
      std::ofstream(tmp / "Assets" / "Tiles" / "noisy.png", std::ios::binary | std::ios::app) << "touched";
    }
    CookResult fourth = cook(target, false);
    CHECK(fourth.ok);
    CHECK_EQ(fourth.assetsCooked, 1u);
    CHECK_EQ(fourth.assetsCached, 2u);
    const std::vector<uint8_t> partlyCached = readAll(fourth.packPath);
    CHECK(partlyCached == cachedPack);  // same pixels, so the same pack
    // --force ignores the cache and must produce the same bytes.
    CookResult forced = cook(target, true);
    CHECK(forced.ok);
    CHECK_EQ(forced.assetsCooked, 3u);
    CHECK_EQ(forced.assetsCached, 0u);
    CHECK(readAll(forced.packPath) == partlyCached);
    // An import setting change misses only that asset's entry.
    const fs::path meta = tmp / "Assets" / "Tiles" / "tileset.png.meta";
    std::string m;
    { std::ifstream in(meta, std::ios::binary); m.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
    const std::string fmt = target;  // any change to importSettings misses the entry; unknown keys are ignored by the importer
    const size_t at = m.find("\"importSettings\"");
    CHECK(at != std::string::npos);
    const size_t open = m.find(static_cast<char>(0x7b), at), close = m.find(static_cast<char>(0x7d), open);
    m.replace(open, close - open + 1, "\x7b\"note\":\"" + fmt + "\"\x7d");
    std::ofstream(meta, std::ios::binary | std::ios::trunc) << m;
    CookResult fifth = cook(target, false);
    CHECK(fifth.ok);
    CHECK_EQ(fifth.assetsCooked, 1u);
    CookResult sixth = cook(target, true);
    CHECK(sixth.ok);
    CHECK(readAll(sixth.packPath) == readAll(fifth.packPath));
    fs::remove_all(tmp / "Library");
  }
  fs::remove_all(tmp);
}
