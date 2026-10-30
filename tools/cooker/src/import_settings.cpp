// F3.02: import settings shared by the cook and the editor: per-target overrides, parsing, texture pre-processing.
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "rensga/cooker/plugin.h"

namespace rensga {

using nlohmann::json;

std::string effectiveImportSettings(const json& meta, const std::string& target) {
  json base = meta.contains("importSettings") && meta["importSettings"].is_object() ? meta["importSettings"] : json::object();
  if (meta.contains("targetOverrides") && meta["targetOverrides"].is_object() && meta["targetOverrides"].contains(target) &&
      meta["targetOverrides"][target].is_object())
    for (auto it = meta["targetOverrides"][target].begin(); it != meta["targetOverrides"][target].end(); ++it) base[it.key()] = it.value();
  return meta.contains("importSettings") || !base.empty() ? base.dump() : std::string();
}

namespace {
bool load(const std::string& text, json& j, std::string& err) {
  j = json::object();
  if (text.empty()) return true;
  try {
    j = json::parse(text);
    if (!j.is_object()) throw std::runtime_error("importSettings must be an object");
  } catch (const std::exception& e) {
    err = std::string("bad importSettings: ") + e.what();
    return false;
  }
  return true;
}
}  // namespace

bool parseTextureImport(const std::string& text, TextureImport& out, std::string& err) {
  json j;
  if (!load(text, j, err)) return false;
  try {
    out.format = j.value("format", std::string("auto"));
    out.maxSize = j.value("maxSize", 0);
    out.filter = j.value("filter", std::string("bilinear"));
    const std::string alpha = j.value("alpha", std::string("keep"));
    if (alpha != "keep" && alpha != "opaque") throw std::runtime_error("alpha must be 'keep' or 'opaque'");
    out.opaque = alpha == "opaque";
    if (out.maxSize < 0 || out.maxSize > 1024) throw std::runtime_error("maxSize must be between 0 and 1024");
    if (out.filter != "point" && out.filter != "bilinear") throw std::runtime_error("filter must be 'point' or 'bilinear'");
  } catch (const std::exception& e) {
    err = std::string("bad importSettings: ") + e.what();
    return false;
  }
  return true;
}

bool parseAudioImport(const std::string& text, AudioImport& out, std::string& err) {
  json j;
  if (!load(text, j, err)) return false;
  try {
    out.mono = j.value("mono", false);
    out.halveRate = j.value("halveRate", false);
    out.smallerAudio = j.value("smallerAudio", false);
    out.loop = j.value("loop", false);
    out.loopStart = j.value("loopStart", static_cast<int64_t>(0));
    out.loopEnd = j.value("loopEnd", static_cast<int64_t>(-1));
    out.sampleRate = j.value("sampleRate", 0u);
    out.streamed = j.value("streamed", false);
  } catch (const std::exception& e) {
    err = std::string("bad importSettings: ") + e.what();
    return false;
  }
  return true;
}

bool parseModelImport(const std::string& text, ModelImport& out, std::string& err) {
  json j;
  if (!load(text, j, err)) return false;
  try {
    out.scale = j.value("scale", 1.0f);
    const std::string n = j.value("normals", std::string("import"));
    if (n != "import" && n != "flat") throw std::runtime_error("normals must be 'import' or 'flat'");
    out.flatNormals = n == "flat";
    if (!(out.scale > 0.0f)) throw std::runtime_error("scale must be greater than 0");
  } catch (const std::exception& e) {
    err = std::string("bad importSettings: ") + e.what();
    return false;
  }
  return true;
}

void applyModelImport(PrimitiveMesh& m, const ModelImport& s) {
  if (s.scale != 1.0f)
    for (Vector3& p : m.positions) p = Vector3(p.x * s.scale, p.y * s.scale, p.z * s.scale);
  if (s.flatNormals) {
    m.normals.resize(m.positions.size());
    for (size_t t = 0; t + 2 < m.positions.size(); t += 3) {
      Vector3 n = Vector3::Cross(m.positions[t + 1] - m.positions[t], m.positions[t + 2] - m.positions[t]).normalized();
      m.normals[t] = m.normals[t + 1] = m.normals[t + 2] = n;
    }
  }
}

void audioResample(PcmAudio& pcm, uint32_t newRate) {
  if (!newRate || !pcm.sampleRate || newRate >= pcm.sampleRate || !pcm.channels) return;
  const uint32_t ch = pcm.channels, inFrames = pcm.frames();
  const uint64_t outFrames = static_cast<uint64_t>(inFrames) * newRate / pcm.sampleRate;
  if (!outFrames) return;
  std::vector<int16_t> o(static_cast<size_t>(outFrames) * ch);
  for (uint64_t f = 0; f < outFrames; ++f) {
    const double pos = static_cast<double>(f) * pcm.sampleRate / newRate;
    const uint32_t i0 = static_cast<uint32_t>(pos), i1 = std::min(i0 + 1, inFrames - 1);
    const double t = pos - i0;
    for (uint32_t c = 0; c < ch; ++c)
      o[f * ch + c] = static_cast<int16_t>(std::lround(pcm.samples[i0 * ch + c] * (1.0 - t) + pcm.samples[i1 * ch + c] * t));
  }
  pcm.samples.swap(o);
  pcm.sampleRate = newRate;
}

bool cookTextureImage(ICookerFormatPlugin& plugin, const ImageRgba& image, const TextureImport& s, CookedTexture& out, std::string& error) {
  if (s.maxSize <= 0 && !s.opaque) return plugin.cookTexture(image, s, out, error);
  ImageRgba img = image;
  const uint32_t longest = std::max(img.width, img.height);
  if (s.maxSize > 0 && longest > static_cast<uint32_t>(s.maxSize)) {
    const double k = static_cast<double>(s.maxSize) / longest;
    const uint32_t nw = std::max(1u, static_cast<uint32_t>(std::lround(img.width * k)));
    const uint32_t nh = std::max(1u, static_cast<uint32_t>(std::lround(img.height * k)));
    ImageRgba r;
    r.width = nw;
    r.height = nh;
    r.rgba.resize(static_cast<size_t>(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; ++y)
      for (uint32_t x = 0; x < nw; ++x) {
        const uint32_t sx = std::min(img.width - 1, static_cast<uint32_t>((x + 0.5) * img.width / nw));
        const uint32_t sy = std::min(img.height - 1, static_cast<uint32_t>((y + 0.5) * img.height / nh));
        for (int c = 0; c < 4; ++c) r.rgba[(static_cast<size_t>(y) * nw + x) * 4 + c] = img.rgba[(static_cast<size_t>(sy) * img.width + sx) * 4 + c];
      }
    img = std::move(r);
  }
  if (s.opaque)
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
  return plugin.cookTexture(img, s, out, error);
}

}  // namespace rensga
