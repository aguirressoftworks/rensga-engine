#include "rensga/editor/import_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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

std::vector<ImportField> importFields(AssetKind kind, const ConsoleTargetData& ct, bool isProjectTarget) {
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
