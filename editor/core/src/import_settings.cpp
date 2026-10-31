#include "rensga/editor/import_settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rensga/cooker/model3d.h"
#include "rensga/cooker/plugin.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

std::vector<std::string> buildTargetIds(const std::string& projectTarget) {
  std::vector<std::string> out;
  const auto& all = CookerPluginRegistry::instance().all();
  if (!projectTarget.empty() && CookerPluginRegistry::instance().find(projectTarget)) out.push_back(projectTarget);
  std::vector<std::string> rest;
  for (ICookerFormatPlugin* p : all)
    if (!p->isProfile() && std::string(p->id()) != projectTarget) rest.push_back(std::string(p->id()));
  std::sort(rest.begin(), rest.end());
  out.insert(out.end(), rest.begin(), rest.end());
  return out;
}

std::vector<ImportField> importFields(AssetKind kind, const ConsoleTargetData& ct, bool isProjectTarget, const ImportRules* rules) {
  std::vector<ImportField> f;
  if (kind == AssetKind::Texture) {
    ImportField fmt;
    fmt.key = "format";
    fmt.label = "Format";
    fmt.type = "enum";
    fmt.options = {"auto", "rgba8"};
    if (isProjectTarget)
      for (const std::string& t : ct.textureFormats)
        if (std::find(fmt.options.begin(), fmt.options.end(), t) == fmt.options.end()) fmt.options.push_back(t);
    fmt.def = "auto";
    fmt.help = "auto picks 4 or 8 bit palettes by colour count where the target has them; clut4 = 16 colours, clut8 = 256 colours.";
    f.push_back(fmt);
    ImportField ms;
    ms.key = "maxSize";
    ms.label = "Max size";
    ms.type = "enum";
    ms.options = {"0", "32", "64", "128", "256", "512", "1024"};
    ms.def = 0;
    ms.help = "Longest edge in pixels. 0 keeps the source size.";
    if (rules && isProjectTarget && rules->maxTextureSize) {
      ms.options.erase(std::remove_if(ms.options.begin() + 1, ms.options.end(),
                                      [&](const std::string& o) { return static_cast<uint32_t>(std::atoi(o.c_str())) > rules->maxTextureSize; }),
                       ms.options.end());
      ms.help = "Longest edge in pixels. 0 keeps the source size, and the cook still scales anything over " + std::to_string(rules->maxTextureSize) +
                " down to fit this target.";
    }
    f.push_back(ms);
    ImportField fl;
    fl.key = "filter";
    fl.label = "Filter";
    fl.type = "enum";
    fl.options = {"bilinear", "point"};
    fl.def = "bilinear";
    fl.help = "Sampling filter, stored for the runtime.";
    f.push_back(fl);
    ImportField al;
    al.key = "alpha";
    al.label = "Alpha";
    al.type = "enum";
    al.options = {"keep", "opaque"};
    al.def = "keep";
    al.help = "opaque drops the alpha channel.";
    f.push_back(al);
  } else if (kind == AssetKind::Audio) {
    ImportField sr;
    sr.key = "sampleRate";
    sr.label = "Sample rate";
    sr.type = "enum";
    sr.options = {"0", "44100", "22050", "11025"};
    sr.def = 0;
    sr.help = "Resamples down to this rate (Hz). 0 keeps the source rate.";
    if (rules && isProjectTarget)
      for (int r : rules->audioRates)
        if (std::find(sr.options.begin(), sr.options.end(), std::to_string(r)) == sr.options.end()) sr.options.push_back(std::to_string(r));
    f.push_back(sr);
    ImportField mono;
    mono.key = "mono";
    mono.label = "Force mono";
    mono.type = "bool";
    mono.def = false;
    f.push_back(mono);
    ImportField st;
    st.key = "streamed";
    st.label = "Streamed";
    st.type = "bool";
    st.def = false;
    st.help = "Marks the clip for streaming from disc. Long music goes in Streaming/ today.";
    f.push_back(st);
    ImportField lp;
    lp.key = "loop";
    lp.label = "Loop";
    lp.type = "bool";
    lp.def = false;
    f.push_back(lp);
  } else if (kind == AssetKind::Model) {
    ImportField sc;
    sc.key = "scale";
    sc.label = "Scale";
    sc.type = "float";
    sc.def = 1.0;
    f.push_back(sc);
    ImportField nm;
    nm.key = "normals";
    nm.label = "Normals";
    nm.type = "enum";
    nm.options = {"import", "flat"};
    nm.def = "import";
    f.push_back(nm);
    if (rules && isProjectTarget && rules->vertexPrecisions.size() > 1) {
      ImportField vp;
      vp.key = "vertexPrecision";
      vp.label = "Vertices";
      vp.type = "enum";
      vp.options = rules->vertexPrecisions;
      vp.def = rules->vertexPrecisions.back();
      vp.help = "float = 32-bit positions, normals and UVs; 16bit = compact vertices (about half the memory, small rounding error).";
      f.push_back(vp);
    }
  }
  return f;
}

json fieldValue(const ImportField& f, const json& settings) {
  if (settings.is_object() && settings.contains(f.key)) return settings[f.key];
  return f.def;
}

static bool readBytes(const fs::path& p, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) return false;
  uint8_t buf[65536];
  size_t n;
  out.clear();
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return true;
}

ImportOutcome AssetImporter::import(const AssetEntry& asset, const std::string& target, const std::string& settingsText, bool useCache) const {
  ImportOutcome r;
  r.kind = asset.kind;
  ICookerFormatPlugin* plugin = CookerPluginRegistry::instance().find(target);
  if (!plugin) { r.error = "no cooker plugin for target '" + target + "'"; return r; }
  if (asset.kind != AssetKind::Texture && asset.kind != AssetKind::Audio) { r.error = "this asset kind has no cooked form to import"; return r; }
  AssetSource src;
  src.path = asset.path;
  src.rel = asset.rel;
  src.guid = asset.guid;
  src.importSettings = settingsText;
  CookedAssets out;
  AssetCache cache;
  if (useCache) {
    cache.dir = root_ / "Library" / "Cache" / target;
    cache.salt = assetCacheSalt(root_, target);
    cache.prune = false;
    std::error_code ec;
    fs::create_directories(cache.dir, ec);
  }
  std::string firstError;
  const bool ok = cookAssets({src}, *plugin, out,
                             [&](CookMessage::Severity sv, const std::string& text, const std::string&, const std::string&) {
                               if (sv == CookMessage::Error && firstError.empty()) firstError = text;
                             },
                             useCache ? &cache : nullptr);
  if (!ok || (asset.kind == AssetKind::Texture && out.textures.size() < 2) || (asset.kind == AssetKind::Audio && out.audio.empty())) {
    r.error = firstError.empty() ? "import failed" : firstError;
    return r;
  }
  r.fromCache = useCache && cache.cached > 0 && cache.cooked == 0;
  if (asset.kind == AssetKind::Texture) {
    const PackTexRec& t = out.textures[1];  // [0] is the default font every pack carries
    r.tex.width = t.width;
    r.tex.height = t.height;
    r.tex.format = t.format;
    r.storedWidth = t.storedW();
    r.storedHeight = t.storedH();
    r.tex.pixels.assign(out.blob.begin() + t.pixelOffset, out.blob.begin() + t.pixelOffset + t.pixelBytes);
    if (t.paletteCount) {
      r.tex.palette.resize(t.paletteCount);
      std::memcpy(r.tex.palette.data(), out.blob.data() + t.paletteOffset, t.paletteCount * 4);
    }
    r.cookedBytes = r.tex.pixels.size() + r.tex.palette.size() * 4;
    std::vector<uint8_t> bytes;
    ImageRgba img;
    std::string e;
    if (readBytes(asset.path, bytes) && decodePng(bytes, img, e)) { r.srcWidth = img.width; r.srcHeight = img.height; }
  } else {
    const PackAudioRec& a = out.audio[0];
    r.audioRate = a.sampleRate;
    r.audioChannels = a.channels;
    r.audioFrames = a.frameCount;
    r.cookedBytes = static_cast<uint64_t>(a.frameCount) * a.channels * 2;
  }
  r.ok = true;
  return r;
}

static bool isPow2(uint32_t v) { return v && !(v & (v - 1)); }
static std::string kbText(uint64_t b) {
  char t[48];
  if (b >= 1024 * 1024) std::snprintf(t, sizeof t, "%.2f MB", b / 1048576.0);
  else std::snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  return t;
}

AssetCost textureCost(const ImportOutcome& o, const ImportRules& rules) {
  AssetCost c;
  if (!o.ok) return c;
  c.valid = true;
  c.vramBytes = o.cookedBytes;
  c.ramBytes = o.cookedBytes;
  char d[160];
  std::snprintf(d, sizeof d, "%u x %u (stored %u x %u), %s", o.tex.width, o.tex.height, o.storedWidth, o.storedHeight, cookedTextureDescription(o.tex).c_str());
  c.detail = d;
  if (o.srcWidth && o.srcHeight && (o.tex.width < o.srcWidth || o.tex.height < o.srcHeight))
    c.notes.push_back("The cook will downscale " + std::to_string(o.srcWidth) + " x " + std::to_string(o.srcHeight) + " to " + std::to_string(o.tex.width) +
                      " x " + std::to_string(o.tex.height) + (rules.maxTextureSize ? " (limit " + std::to_string(rules.maxTextureSize) + ")" : std::string()) + ".");
  if (rules.pow2Warn) {
    const uint32_t w = o.srcWidth ? std::min(o.srcWidth, o.tex.width) : o.tex.width, h = o.srcHeight ? std::min(o.srcHeight, o.tex.height) : o.tex.height;
    if (!isPow2(w) || !isPow2(h)) {
      const uint64_t wasted = o.storedWidth && o.storedHeight ? static_cast<uint64_t>(o.storedWidth) * o.storedHeight - static_cast<uint64_t>(o.tex.width) * o.tex.height : 0;
      c.notes.push_back(std::to_string(w) + " x " + std::to_string(h) + " is not a power of two: stored as " + std::to_string(o.storedWidth) + " x " +
                        std::to_string(o.storedHeight) + (wasted ? " (" + std::to_string(wasted) + " padded texels)" : std::string()) + ".");
    }
  }
  return c;
}

AssetCost audioCost(const ImportOutcome& o) {
  AssetCost c;
  if (!o.ok) return c;
  c.valid = true;
  c.ramBytes = o.cookedBytes;
  c.detail = std::to_string(o.audioRate) + " Hz, " + std::to_string(o.audioChannels) + " channel(s), " + kbText(o.cookedBytes);
  return c;
}

AssetCost modelCost(const fs::path& file, const ConsoleTargetData& ct, const std::string& vertexPrecision, bool compactCapable) {
  AssetCost c;
  GltfModel g;
  std::string err;
  ModelImport mi;
  if (!importGltfModel(file, mi, true, 30, g, err)) { c.notes.push_back("Cannot read the model: " + err); return c; }
  const uint64_t verts = g.mesh.positions.size();
  const bool compact = compactCapable && vertexPrecision != "float" && uvFitsC16(g.mesh);
  const uint64_t stride = g.skinned ? (compact ? 28 : 44) : (compact ? 20 : 36);
  c.valid = true;
  c.ramBytes = verts * stride;
  uint32_t batches = 1, dropped = 0;
  if (g.skinned && ct.maxBonesPerDraw) {
    SkinSplitResult sr;
    std::string w, e;
    LitMesh in = g.mesh;
    in.weights.assign(g.mesh.positions.size(), std::array<uint8_t, 8>{});
    // weights are set by the split from the influences
    if (splitSkinnedMesh(in, g.influences, ct.maxBonesPerDraw, sr, w, e)) {
      batches = static_cast<uint32_t>(sr.batches.size());
      dropped = sr.droppedInfluences;
      c.ramBytes = static_cast<uint64_t>(sr.mesh.positions.size()) * stride + sr.batches.size() * sizeof(PackBoneBatch);
    }
  }
  char d[200];
  std::snprintf(d, sizeof d, "%llu vertices, %llu triangles, %s vertices (%llu bytes each)", static_cast<unsigned long long>(verts),
                static_cast<unsigned long long>(verts / 3), compact ? "16-bit" : "float", static_cast<unsigned long long>(stride));
  c.detail = d;
  if (g.skinned) {
    c.detail += ", " + std::to_string(g.joints.size()) + " joints, " + std::to_string(batches) + " bone batch(es), " + std::to_string(g.clips.size()) + " clip(s)";
    if (ct.maxSkinnedJoints && g.joints.size() > ct.maxSkinnedJoints)
      c.notes.push_back(std::to_string(g.joints.size()) + " joints is over the limit of " + std::to_string(ct.maxSkinnedJoints) + ": the cook refuses this skin.");
    if (ct.maxBonesPerDraw && batches > 1)
      c.notes.push_back("The skin is split into " + std::to_string(batches) + " batches of at most " + std::to_string(ct.maxBonesPerDraw) + " bones (one draw each).");
    if (dropped) c.notes.push_back(std::to_string(dropped) + " influences are dropped to fit the batches.");
  }
  if (compactCapable && vertexPrecision != "float" && !uvFitsC16(g.mesh)) c.notes.push_back("UVs outside [0, 2) do not fit 16-bit vertices: this mesh stays float.");
  for (const std::string& w : g.warnings) c.notes.push_back(w);
  return c;
}

std::vector<uint8_t> cookedTextureToRgba(const CookedTexture& t) {
  std::vector<uint8_t> out(static_cast<size_t>(t.width) * t.height * 4);
  if (t.format == 0) {
    std::copy(t.pixels.begin(), t.pixels.begin() + std::min(t.pixels.size(), out.size()), out.begin());
    return out;
  }
  const bool four = t.format == 1;
  const size_t rowBytes = four ? (t.width + 1) / 2 : t.width;
  for (uint32_t y = 0; y < t.height; ++y)
    for (uint32_t x = 0; x < t.width; ++x) {
      uint32_t idx;
      const size_t at = y * rowBytes + (four ? x / 2 : x);
      if (at >= t.pixels.size()) idx = 0;
      else idx = four ? ((x & 1) ? t.pixels[at] >> 4 : t.pixels[at] & 15) : t.pixels[at];
      const uint32_t c = idx < t.palette.size() ? t.palette[idx] : 0;
      uint8_t* o = &out[(static_cast<size_t>(y) * t.width + x) * 4];
      o[0] = c & 255; o[1] = (c >> 8) & 255; o[2] = (c >> 16) & 255; o[3] = (c >> 24) & 255;
    }
  return out;
}

std::string cookedTextureDescription(const CookedTexture& t) {
  if (t.format == 0) return "RGBA8";
  return std::to_string(t.palette.size()) + " colours, " + (t.format == 1 ? "4" : "8") + "-bit";
}

}  // namespace editor
}  // namespace rensga
