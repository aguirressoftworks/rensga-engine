// F2.03: turns the project's textures, audio, tilemaps, sprite clips and fonts into pack-v2 payloads.
// Order is by project path, so the output is deterministic. Every error names the asset and GUID.
#include <string.h>

#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <map>
#include <set>
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

// ---- per-asset cache entries (host-endian; a cache is never shared between machines) ----
uint64_t fnvBytes(const void* data, size_t n, uint64_t h) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}
std::string cacheKey(const AssetCache& c, const char* kind, const std::vector<uint8_t>& src, const std::string& settings) {
  const std::string head = c.salt + '\0' + kind + '\0' + settings + '\0';
  uint64_t out[2];
  const uint64_t seeds[2] = {1469598103934665603ull, 0x9E3779B97F4A7C15ull};
  for (int i = 0; i < 2; ++i) {
    uint64_t h = fnvBytes(head.data(), head.size(), seeds[i]);
    out[i] = fnvBytes(src.data(), src.size(), h);
  }
  char b[33];
  snprintf(b, sizeof b, "%016llx%016llx", static_cast<unsigned long long>(out[0]), static_cast<unsigned long long>(out[1]));
  return b;
}

struct CacheWriter {
  std::vector<uint8_t> b;
  void u32v(uint32_t v) { raw(&v, 4); }
  void raw(const void* p, size_t n) { const uint8_t* q = static_cast<const uint8_t*>(p); b.insert(b.end(), q, q + n); }
  bool save(const fs::path& path) {
    const uint64_t sum = fnvBytes(b.data(), b.size(), 1469598103934665603ull);
    raw(&sum, 8);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary);
      if (!out) return false;
      out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
      if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
  }
};
struct CacheReader {
  std::vector<uint8_t> b;
  size_t pos = 0;
  bool bad = false;
  bool load(const fs::path& path) {
    if (!readBytes(path, b) || b.size() < 12) return false;
    uint64_t sum;
    memcpy(&sum, b.data() + b.size() - 8, 8);
    b.resize(b.size() - 8);
    return sum == fnvBytes(b.data(), b.size(), 1469598103934665603ull);
  }
  uint32_t u32v() { uint32_t v = 0; raw(&v, 4); return v; }
  void raw(void* p, size_t n) {
    if (bad || b.size() - pos < n) { bad = true; return; }
    if (n) memcpy(p, b.data() + pos, n);
    pos += n;
  }
};
constexpr uint32_t kMagicTex = 0x32544752u, kMagicAud = 0x41434752u;  // "RGT2", "RGCA"

void storeTex(const fs::path& path, const CookedTexture& ct, uint32_t srcW, uint32_t srcH) {
  CacheWriter w;
  w.u32v(kMagicTex); w.u32v(srcW); w.u32v(srcH); w.u32v(ct.width); w.u32v(ct.height); w.u32v(ct.format);
  w.u32v(ct.storedWidth); w.u32v(ct.storedHeight); w.u32v(ct.flags);
  w.u32v(static_cast<uint32_t>(ct.pixels.size())); w.u32v(static_cast<uint32_t>(ct.palette.size()));
  w.raw(ct.pixels.data(), ct.pixels.size());
  w.raw(ct.palette.data(), ct.palette.size() * 4);
  w.save(path);
}
bool loadTex(const fs::path& path, CookedTexture& ct, uint32_t& srcW, uint32_t& srcH) {
  CacheReader r;
  if (!r.load(path) || r.u32v() != kMagicTex) return false;
  srcW = r.u32v(); srcH = r.u32v(); ct.width = r.u32v(); ct.height = r.u32v(); ct.format = r.u32v();
  ct.storedWidth = r.u32v(); ct.storedHeight = r.u32v(); ct.flags = r.u32v();
  const uint32_t np = r.u32v(), nc = r.u32v();
  if (r.bad || np > r.b.size() || nc > r.b.size()) return false;
  ct.pixels.resize(np);
  ct.palette.resize(nc);
  r.raw(ct.pixels.data(), np);
  r.raw(ct.palette.data(), static_cast<size_t>(nc) * 4);
  return !r.bad && r.pos == r.b.size();
}
void storeAud(const fs::path& path, const PcmAudio& pcm, uint32_t srcFrames, uint32_t srcRate) {
  CacheWriter w;
  w.u32v(kMagicAud); w.u32v(srcFrames); w.u32v(srcRate); w.u32v(pcm.sampleRate); w.u32v(pcm.channels);
  w.u32v(static_cast<uint32_t>(pcm.samples.size()));
  w.raw(pcm.samples.data(), pcm.samples.size() * 2);
  w.save(path);
}
bool loadAud(const fs::path& path, PcmAudio& pcm, uint32_t& srcFrames, uint32_t& srcRate) {
  CacheReader r;
  if (!r.load(path) || r.u32v() != kMagicAud) return false;
  srcFrames = r.u32v(); srcRate = r.u32v(); pcm.sampleRate = r.u32v(); pcm.channels = r.u32v();
  const uint32_t n = r.u32v();
  if (r.bad || n > r.b.size()) return false;
  pcm.samples.resize(n);
  r.raw(pcm.samples.data(), static_cast<size_t>(n) * 2);
  return !r.bad && r.pos == r.b.size();
}

}  // namespace

bool cookAssets(const std::vector<AssetSource>& sourcesIn, ICookerFormatPlugin& plugin, CookedAssets& out,
                const std::function<void(CookMessage::Severity, const std::string&, const std::string&, const std::string&)>& report,
                AssetCache* cache) {
  bool ok = true;
  if (cache && cache->dir.empty()) cache = nullptr;
  std::set<std::string> usedEntries;  // cache files this cook needs; the rest are pruned when the cook succeeds
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

  auto addCooked = [&](const AssetSource& a, const CookedTexture& ct, uint32_t srcW, uint32_t srcH) {
    PackTexRec r = {};
    Guid::parse(a.guid.c_str(), r.guid);
    r.width = ct.width;
    r.height = ct.height;
    r.format = ct.format;
    r.storedWidth = static_cast<uint16_t>(ct.storedWidth);
    r.storedHeight = static_cast<uint16_t>(ct.storedHeight);
    r.flags = static_cast<uint16_t>(ct.flags);
    r.pixelBytes = static_cast<uint32_t>(ct.pixels.size());
    r.pixelOffset = blobAdd(out.blob, ct.pixels.data(), ct.pixels.size());
    if (!ct.palette.empty()) {
      r.paletteCount = static_cast<uint16_t>(ct.palette.size());
      r.paletteOffset = blobAdd(out.blob, ct.palette.data(), ct.palette.size() * 4);
    }
    out.textureBytes += ct.pixels.size() + ct.palette.size() * 4;
    texByGuid[a.guid] = {static_cast<uint32_t>(out.textures.size()), srcW, srcH};
    out.textures.push_back(r);
  };
  auto addTexture = [&](const AssetSource& a, const ImageRgba& img, const TextureImport& ti) {
    CookedTexture ct;
    std::string err;
    if (!plugin.cookTexture(img, ti, ct, err)) {
      fail(a, err);
      return;
    }
    addCooked(a, ct, img.width, img.height);
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
    std::string entry;
    if (cache) {
      entry = cacheKey(*cache, "tex", bytes, a.importSettings) + ".tex";
      CookedTexture hit;
      uint32_t sw = 0, sh = 0;
      if (cache->reuse && loadTex(cache->dir / entry, hit, sw, sh)) {
        usedEntries.insert(entry);
        ++cache->cached;
        addCooked(a, hit, sw, sh);
        continue;
      }
    }
    if (!decodePng(bytes, img, err)) { fail(a, err); continue; }
    if (img.width == 0 || img.height == 0 || img.width > 1024 || img.height > 1024) {
      fail(a, "texture is " + std::to_string(img.width) + "x" + std::to_string(img.height) + ", the limit is 1024x1024");
      continue;
    }
    TextureImport ti;
    if (!parseTextureImport(a.importSettings, ti, err)) { fail(a, err); continue; }
    CookedTexture ct;
    if (!cookTextureImage(plugin, img, ti, ct, err)) { fail(a, err); continue; }
    if (cache) {
      storeTex(cache->dir / entry, ct, img.width, img.height);
      usedEntries.insert(entry);
      ++cache->cooked;
    }
    addCooked(a, ct, img.width, img.height);
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
    AudioImport ai;
    if (!parseAudioImport(a.importSettings, ai, err)) { fail(a, err); continue; }
    uint32_t srcFrames = 0, srcRate = 0;
    std::string entry;
    bool hit = false;
    if (cache) {
      entry = cacheKey(*cache, "aud", bytes, a.importSettings) + ".aud";
      hit = cache->reuse && loadAud(cache->dir / entry, pcm, srcFrames, srcRate);
    }
    if (hit) {
      ++cache->cached;
    } else {
      if (!parseWav(bytes, pcm, err)) { fail(a, err); continue; }
      srcFrames = pcm.frames();
      srcRate = pcm.sampleRate;
      if (!plugin.cookAudio(pcm, ai, err)) { fail(a, err); continue; }
      if (cache) {
        storeAud(cache->dir / entry, pcm, srcFrames, srcRate);
        ++cache->cooked;
      }
    }
    if (cache) usedEntries.insert(entry);
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
        // Tile flags (pack-format.md): "solid" [ids], "oneWay" [ids], "slopes" {"<id>": "45L"|"45R"|"22L_lo"|...}.
        // A tile may have only one of the three; a tilemap with only "solid" cooks to the original 1-per-tile bytes.
        std::vector<uint8_t> solid(tileCount, 0);
        auto setFlag = [&](uint32_t id, uint8_t flag, const char* what) {
          if (id >= tileCount) { fail(a, std::string(what) + " tile id " + std::to_string(id) + " is outside the tileset"); bad = true; return; }
          if (solid[id] && solid[id] != flag) { fail(a, "tile id " + std::to_string(id) + " has more than one of solid, oneWay and slopes"); bad = true; return; }
          solid[id] = flag;
        };
        for (const json& sj : j.value("solid", json::array())) { setFlag(sj.get<uint32_t>(), PackTileFlag::kSolid, "solid"); if (bad) break; }
        if (!bad)
          for (const json& sj : j.value("oneWay", json::array())) { setFlag(sj.get<uint32_t>(), PackTileFlag::kOneWay, "oneWay"); if (bad) break; }
        if (!bad && j.contains("slopes")) {
          static const struct { const char* name; uint8_t code; } kSlopes[] = {
              {"45L", kSlope45L}, {"45R", kSlope45R}, {"22L_lo", kSlope22L_lo}, {"22L_hi", kSlope22L_hi},
              {"22R_lo", kSlope22R_lo}, {"22R_hi", kSlope22R_hi}};
          for (auto it = j["slopes"].begin(); it != j["slopes"].end() && !bad; ++it) {
            uint8_t code = 0;
            const std::string name = it.value().get<std::string>();
            for (const auto& k : kSlopes) if (name == k.name) code = k.code;
            if (!code) { fail(a, "unknown slope \"" + name + "\" (use 45L, 45R, 22L_lo, 22L_hi, 22R_lo, 22R_hi)"); bad = true; break; }
            setFlag(static_cast<uint32_t>(std::stoul(it.key())), static_cast<uint8_t>(code << PackTileFlag::kSlopeShift), "slope");
          }
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
  if (cache && ok && cache->prune) {  // drop entries no asset needs any more (changed or deleted sources)
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(cache->dir, ec)) {
      const std::string name = e.path().filename().string();
      if ((endsWith(name, ".tex") || endsWith(name, ".aud") || endsWith(name, ".tmp")) && !usedEntries.count(name)) fs::remove(e.path(), ec);
    }
  }
  return ok;
}

}  // namespace rensga

namespace rensga {

bool cookPhysics2D(const std::string& text, PackPhysics2D& out, std::string& error) {
  for (uint32_t& m : out.masks) m = 0xFFFFFFFFu;
  try {
    const json j = json::parse(text);
    if (j.value("format", 0) != 1) { error = "unsupported format version"; return false; }
    if (j.contains("matrix")) {
      if (!j["matrix"].is_array() || j["matrix"].size() != 32) { error = "\"matrix\" needs 32 bitmasks"; return false; }
      for (size_t i = 0; i < 32; ++i) out.masks[i] = j["matrix"][i].get<uint32_t>();
    }
    std::vector<std::string> names;
    for (const json& n : j.value("layers", json::array())) names.push_back(n.get<std::string>());
    if (names.size() > 32) { error = "more than 32 layer names"; return false; }
    auto layerOf = [&](const json& v, uint32_t& idx) {
      if (v.is_number_unsigned() || v.is_number_integer()) { idx = v.get<uint32_t>(); return idx < 32; }
      const std::string n = v.get<std::string>();
      for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) { idx = static_cast<uint32_t>(i); return true; }
      return false;
    };
    for (const json& pr : j.value("ignore", json::array())) {
      uint32_t x = 0, y = 0;
      if (!pr.is_array() || pr.size() != 2 || !layerOf(pr[0], x) || !layerOf(pr[1], y)) { error = "bad \"ignore\" pair " + pr.dump(); return false; }
      out.masks[x] &= ~(1u << y);
      out.masks[y] &= ~(1u << x);
    }
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
  return true;
}

}  // namespace rensga
