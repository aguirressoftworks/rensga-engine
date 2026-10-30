// F2.03: turns the project's textures, audio, tilemaps, sprite clips and fonts into pack-v2 payloads.
// Order is by project path, so the output is deterministic. Every error names the asset and GUID.
#include <string.h>

#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace rensga {
namespace {

bool endsWith(const std::string& s, const char* suf) {
  size_t n = strlen(suf);
  return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

bool readBytes(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

uint32_t blobAdd(std::vector<uint8_t>& blob, const void* data, size_t bytes) {
  while (blob.size() % 16) blob.push_back(0);
  uint32_t off = static_cast<uint32_t>(blob.size());
  const uint8_t* p = static_cast<const uint8_t*>(data);
  blob.insert(blob.end(), p, p + bytes);
  return off;
}

struct TexInfo { uint32_t index, w, h; };

}  // namespace

bool cookAssets(const std::vector<AssetSource>& sourcesIn, ICookerFormatPlugin& plugin, CookedAssets& out,
                const std::function<void(CookMessage::Severity, const std::string&, const std::string&, const std::string&)>& report) {
  bool ok = true;
  auto fail = [&](const AssetSource& a, const std::string& what) {
    report(CookMessage::Error, a.rel + " (guid " + a.guid + "): " + what, a.rel, a.guid);
    ok = false;
  };
  std::vector<AssetSource> sources = sourcesIn;
  std::sort(sources.begin(), sources.end(), [](const AssetSource& a, const AssetSource& b) { return a.rel < b.rel; });
  std::map<std::string, TexInfo> texByGuid;

  auto settingsOf = [&](const AssetSource& a, json& j) {
    j = json::object();
    if (a.importSettings.empty()) return true;
    try {
      j = json::parse(a.importSettings);
      if (!j.is_object()) throw std::runtime_error("importSettings must be an object");
    } catch (const std::exception& e) {
      fail(a, std::string("bad importSettings: ") + e.what());
      return false;
    }
    return true;
  };

  auto addTexture = [&](const AssetSource& a, const ImageRgba& img, const TextureImport& ti) {
    CookedTexture ct;
    std::string err;
    if (!plugin.cookTexture(img, ti, ct, err)) {
      fail(a, err);
      return;
    }
    PackTexRec r = {};
    Guid::parse(a.guid.c_str(), r.guid);
    r.width = ct.width;
    r.height = ct.height;
    r.format = ct.format;
    r.pixelBytes = static_cast<uint32_t>(ct.pixels.size());
    r.pixelOffset = blobAdd(out.blob, ct.pixels.data(), ct.pixels.size());
    if (!ct.palette.empty()) {
      r.paletteCount = static_cast<uint32_t>(ct.palette.size());
      r.paletteOffset = blobAdd(out.blob, ct.palette.data(), ct.palette.size() * 4);
    }
    out.textureBytes += ct.pixels.size() + ct.palette.size() * 4;
    texByGuid[a.guid] = {static_cast<uint32_t>(out.textures.size()), img.width, img.height};
    out.textures.push_back(r);
  };

  // Pass 1: textures (PNG) and the default font's texture. The default font comes first so its
  // index is stable (0) in every pack.
  {
    AssetSource df;
    df.rel = "<default font>";
    df.guid = kDefaultFontTextureGuid;
    addTexture(df, defaultFontImage(), TextureImport());
  }
  for (const AssetSource& a : sources) {
    if (!endsWith(a.rel, ".png")) continue;
    json s;
    if (!settingsOf(a, s)) continue;
    std::vector<uint8_t> bytes;
    ImageRgba img;
    std::string err;
    if (!readBytes(a.path, bytes)) { fail(a, "cannot read file"); continue; }
    if (!decodePng(bytes, img, err)) { fail(a, err); continue; }
    if (img.width == 0 || img.height == 0 || img.width > 1024 || img.height > 1024) {
      fail(a, "texture is " + std::to_string(img.width) + "x" + std::to_string(img.height) + ", the limit is 1024x1024");
      continue;
    }
    TextureImport ti;
    ti.format = s.value("format", std::string("auto"));
    addTexture(a, img, ti);
  }

  // Pass 2: audio.
  for (const AssetSource& a : sources) {
    if (!endsWith(a.rel, ".wav")) continue;
    json s;
    if (!settingsOf(a, s)) continue;
    std::vector<uint8_t> bytes;
    PcmAudio pcm;
    std::string err;
    if (!readBytes(a.path, bytes)) { fail(a, "cannot read file"); continue; }
    if (!parseWav(bytes, pcm, err)) { fail(a, err); continue; }
    AudioImport ai;
    ai.mono = s.value("mono", false);
    ai.halveRate = s.value("halveRate", false);
    ai.smallerAudio = s.value("smallerAudio", false);
    ai.loop = s.value("loop", false);
    ai.loopStart = s.value("loopStart", static_cast<int64_t>(0));
    ai.loopEnd = s.value("loopEnd", static_cast<int64_t>(-1));
    const uint32_t srcFrames = pcm.frames();
    const uint32_t srcRate = pcm.sampleRate;
    if (!plugin.cookAudio(pcm, ai, err)) { fail(a, err); continue; }
    const uint32_t frames = pcm.frames();
    // Loop points are authored in source frames; rescale if the plugin shortened the clip.
    auto scale = [&](int64_t f) { return srcFrames ? static_cast<uint32_t>(f * frames / srcFrames) : 0u; };
    PackAudioRec r = {};
    Guid::parse(a.guid.c_str(), r.guid);
    r.sampleRate = pcm.sampleRate ? pcm.sampleRate : srcRate;
    r.channels = pcm.channels;
    r.frameCount = frames;
    r.loops = ai.loop ? 1 : 0;
    r.loopStart = scale(ai.loopStart);
    r.loopEnd = ai.loopEnd < 0 ? frames : std::min(frames, scale(ai.loopEnd));
    if (ai.loop && r.loopStart >= r.loopEnd) { fail(a, "loop start is not before loop end"); continue; }
    r.dataOffset = blobAdd(out.blob, pcm.samples.data(), pcm.samples.size() * 2);
    out.audio.push_back(r);
  }

  // Pass 3: JSON assets that point at textures.
  auto texOf = [&](const AssetSource& a, const json& j, const char* key, TexInfo& ti) {
    std::string g = j.value(key, std::string());
    auto it = texByGuid.find(g);
    if (it == texByGuid.end()) {
      fail(a, std::string("'") + key + "' references GUID " + (g.empty() ? "<missing>" : g) + ", which is not a texture in this project");
      return false;
    }
    ti = it->second;
    return true;
  };
  for (const AssetSource& a : sources) {
    const bool isMap = endsWith(a.rel, ".tilemap.json"), isAnim = endsWith(a.rel, ".anim.json"), isFont = endsWith(a.rel, ".font.json");
    if (!isMap && !isAnim && !isFont) continue;
    json j;
    try {
      std::vector<uint8_t> bytes;
      if (!readBytes(a.path, bytes)) throw std::runtime_error("cannot read file");
      j = json::parse(bytes.begin(), bytes.end());
      if (j.value("format", 0) != 1) throw std::runtime_error("unsupported format version (expected \"format\": 1)");
    } catch (const std::exception& e) {
      fail(a, e.what());
      continue;
    }
    try {
      TexInfo ti;
      if (isMap) {
        if (!texOf(a, j, "tileset", ti)) continue;
        PackTilemapRec r = {};
        Guid::parse(a.guid.c_str(), r.guid);
        r.width = j.at("width").get<uint32_t>();
        r.height = j.at("height").get<uint32_t>();
        r.tileSize = j.at("tileSize").get<uint32_t>();
        r.tilesetTexture = ti.index;
        if (!r.tileSize || ti.w % r.tileSize || ti.h % r.tileSize) {
          fail(a, "tileSize " + std::to_string(r.tileSize) + " does not divide the tileset (" + std::to_string(ti.w) + "x" + std::to_string(ti.h) + ")");
          continue;
        }
        const uint32_t tileCount = (ti.w / r.tileSize) * (ti.h / r.tileSize);
        std::vector<uint16_t> tiles;
        for (const json& t : j.at("tiles")) tiles.push_back(static_cast<uint16_t>(t.get<uint32_t>()));
        if (tiles.size() != static_cast<size_t>(r.width) * r.height) {
          fail(a, "tiles has " + std::to_string(tiles.size()) + " entries, width*height is " + std::to_string(static_cast<size_t>(r.width) * r.height));
          continue;
        }
        bool bad = false;
        for (uint16_t t : tiles)
          if (t != 0xFFFF && t >= tileCount) {
            fail(a, "tile id " + std::to_string(t) + " is outside the tileset (" + std::to_string(tileCount) + " tiles; 65535 means empty)");
            bad = true;
            break;
          }
        if (bad) continue;
        std::vector<uint8_t> solid(tileCount, 0);
        for (const json& sj : j.value("solid", json::array())) {
          uint32_t id = sj.get<uint32_t>();
          if (id >= tileCount) { fail(a, "solid tile id " + std::to_string(id) + " is outside the tileset"); bad = true; break; }
          solid[id] = 1;
        }
        if (bad) continue;
        r.tilesOffset = blobAdd(out.blob, tiles.data(), tiles.size() * 2);
        r.solidCount = tileCount;
        r.solidOffset = blobAdd(out.blob, solid.data(), solid.size());
        out.tilemaps.push_back(r);
      } else if (isAnim) {
        if (!texOf(a, j, "texture", ti)) continue;
        std::vector<PackFrameRect> frames;
        if (j.contains("grid")) {
          const json& g = j["grid"];
          uint32_t cw = g.at("cellW").get<uint32_t>(), ch = g.at("cellH").get<uint32_t>();
          uint32_t start = g.value("start", 0u), count = g.at("count").get<uint32_t>();
          if (!cw || !ch || cw > ti.w || ch > ti.h) { fail(a, "grid cell does not fit the texture"); continue; }
          uint32_t cols = ti.w / cw;
          for (uint32_t i = 0; i < count; ++i) {
            uint32_t c = start + i;
            frames.push_back({static_cast<uint16_t>((c % cols) * cw), static_cast<uint16_t>((c / cols) * ch),
                              static_cast<uint16_t>(cw), static_cast<uint16_t>(ch)});
          }
        } else {
          for (const json& f : j.at("frames"))
            frames.push_back({f.at(0).get<uint16_t>(), f.at(1).get<uint16_t>(), f.at(2).get<uint16_t>(), f.at(3).get<uint16_t>()});
        }
        bool bad = frames.empty();
        if (bad) fail(a, "clip has no frames");
        for (const PackFrameRect& f : frames)
          if (!bad && (f.w == 0 || f.h == 0 || f.x + f.w > ti.w || f.y + f.h > ti.h)) {
            fail(a, "frame rect " + std::to_string(f.x) + "," + std::to_string(f.y) + "," + std::to_string(f.w) + "," + std::to_string(f.h) + " is outside the texture");
            bad = true;
          }
        if (bad) continue;
        PackAnimRec r = {};
        Guid::parse(a.guid.c_str(), r.guid);
        r.texture = ti.index;
        r.frameCount = static_cast<uint32_t>(frames.size());
        r.framesOffset = blobAdd(out.blob, frames.data(), frames.size() * sizeof(PackFrameRect));
        r.fps = j.value("fps", 12u);
        r.loop = j.value("loop", true) ? 1 : 0;
        out.anims.push_back(r);
        out.animNames.push_back(j.value("name", fs::path(a.rel).stem().stem().string()));
      } else {
        if (!texOf(a, j, "texture", ti)) continue;
        PackFontRec r = {};
        Guid::parse(a.guid.c_str(), r.guid);
        r.texture = ti.index;
        r.glyphW = j.at("glyphW").get<uint16_t>();
        r.glyphH = j.at("glyphH").get<uint16_t>();
        r.firstChar = j.value("firstChar", 32u);
        r.charCount = j.at("charCount").get<uint16_t>();
        r.columns = j.value("columns", r.glyphW ? ti.w / r.glyphW : 0u);
        r.lineHeight = j.value("lineHeight", static_cast<uint32_t>(r.glyphH));
        if (!r.glyphW || !r.glyphH || !r.columns || r.charCount == 0 || r.charCount > kPackFontMaxChars ||
            static_cast<uint32_t>(r.columns * r.glyphW) > ti.w || static_cast<uint32_t>((r.charCount + r.columns - 1) / r.columns) * r.glyphH > ti.h) {
          fail(a, "glyph grid does not fit the texture or has more than " + std::to_string(kPackFontMaxChars) + " glyphs");
          continue;
        }
        if (j.contains("advance") && j["advance"].is_array()) {
          if (j["advance"].size() != r.charCount) { fail(a, "advance has to list one value per glyph"); continue; }
          for (size_t i = 0; i < r.charCount; ++i) r.advance[i] = j["advance"][i].get<uint8_t>();
        } else {
          uint8_t adv = static_cast<uint8_t>(j.value("advance", static_cast<uint32_t>(r.glyphW)));
          for (size_t i = 0; i < r.charCount; ++i) r.advance[i] = adv;
        }
        out.fonts.push_back(r);
      }
    } catch (const std::exception& e) {
      fail(a, e.what());
    }
  }

  // The default font is always font 0 of every pack.
  {
    PackFontRec r = {};
    Guid::parse(kDefaultFontGuid, r.guid);
    r.texture = 0;
    r.glyphW = r.glyphH = 8;
    r.firstChar = 32;
    r.charCount = 95;
    r.columns = 16;
    r.lineHeight = 9;
    for (size_t i = 0; i < r.charCount; ++i) r.advance[i] = 8;
    out.fonts.insert(out.fonts.begin(), r);
  }
  return ok;
}

}  // namespace rensga
