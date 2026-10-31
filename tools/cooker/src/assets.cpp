// F2.03: importers (PNG, WAV, glTF) and shared texture/audio helpers.
#include "rensga/cooker/assets.h"

#include <string.h>

#include <algorithm>
#include <cmath>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "rensga/cooker/model3d.h"
#include "rensga/cooker/plugin.h"

namespace rensga {

// ---- PNG ----------------------------------------------------------------------------------------
bool decodePng(const std::vector<uint8_t>& bytes, ImageRgba& out, std::string& error) {
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
  if (!px) {
    error = std::string("PNG decode failed: ") + stbi_failure_reason();
    return false;
  }
  out.width = static_cast<uint32_t>(w);
  out.height = static_cast<uint32_t>(h);
  out.rgba.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  stbi_image_free(px);
  return true;
}

// ---- WAV ----------------------------------------------------------------------------------------
namespace {
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
}  // namespace

bool parseWav(const std::vector<uint8_t>& b, PcmAudio& out, std::string& error) {
  if (b.size() < 12 || memcmp(b.data(), "RIFF", 4) != 0 || memcmp(b.data() + 8, "WAVE", 4) != 0) {
    error = "not a RIFF/WAVE file";
    return false;
  }
  uint32_t tag = 0, channels = 0, rate = 0, bits = 0;
  const uint8_t* data = nullptr;
  uint32_t dataSize = 0;
  size_t pos = 12;
  while (pos + 8 <= b.size()) {
    uint32_t sz = rd32(&b[pos + 4]);
    const uint8_t* body = &b[pos + 8];
    size_t avail = b.size() - (pos + 8);
    if (sz > avail) sz = static_cast<uint32_t>(avail);  // truncated final chunk
    if (memcmp(&b[pos], "fmt ", 4) == 0 && sz >= 16) {
      tag = rd16(body);
      channels = rd16(body + 2);
      rate = rd32(body + 4);
      bits = rd16(body + 14);
      if (tag == 0xFFFE && sz >= 26) tag = rd16(body + 24);  // WAVE_FORMAT_EXTENSIBLE: sub-format
    } else if (memcmp(&b[pos], "data", 4) == 0) {
      data = body;
      dataSize = sz;
    }
    pos += 8 + static_cast<size_t>(sz) + (sz & 1);
  }
  if (!channels) {
    error = "WAV has no fmt chunk";
    return false;
  }
  if (!data) {
    error = "WAV has no data chunk";
    return false;
  }
  if (tag != 1 || (bits != 8 && bits != 16)) {
    error = "only PCM 8-bit or 16-bit WAV is supported (found format " + std::to_string(tag) + ", " + std::to_string(bits) + " bits)";
    return false;
  }
  if (channels > 2 || rate == 0) {
    error = "WAV must be mono or stereo with a sample rate";
    return false;
  }
  uint32_t bytesPer = bits / 8;
  uint32_t frames = dataSize / (bytesPer * channels);
  out.sampleRate = rate;
  out.channels = channels;
  out.samples.resize(static_cast<size_t>(frames) * channels);
  for (size_t i = 0; i < out.samples.size(); ++i) {
    if (bits == 16) out.samples[i] = static_cast<int16_t>(rd16(data + i * 2));
    else out.samples[i] = static_cast<int16_t>((static_cast<int>(data[i]) - 128) * 256);
  }
  return true;
}

void audioDownmixMono(PcmAudio& pcm) {
  if (pcm.channels != 2) return;
  std::vector<int16_t> m(pcm.samples.size() / 2);
  for (size_t i = 0; i < m.size(); ++i) m[i] = static_cast<int16_t>((pcm.samples[i * 2] + pcm.samples[i * 2 + 1]) / 2);
  pcm.samples.swap(m);
  pcm.channels = 1;
}

void audioHalveRate(PcmAudio& pcm) {
  uint32_t frames = pcm.frames() / 2, ch = pcm.channels;
  if (!frames) return;
  std::vector<int16_t> h(static_cast<size_t>(frames) * ch);
  for (uint32_t f = 0; f < frames; ++f)
    for (uint32_t c = 0; c < ch; ++c)
      h[f * ch + c] = static_cast<int16_t>((pcm.samples[(f * 2) * ch + c] + pcm.samples[(f * 2 + 1) * ch + c]) / 2);
  pcm.samples.swap(h);
  pcm.sampleRate = pcm.sampleRate / 2;
}

// ---- textures -----------------------------------------------------------------------------------
CookedTexture makeRgba8(const ImageRgba& img) {
  CookedTexture t;
  t.width = img.width;
  t.height = img.height;
  t.format = 0;
  t.pixels = img.rgba;
  return t;
}

namespace {
uint32_t packRgba(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
int chan(uint32_t c, int k) { return static_cast<int>((c >> (8 * k)) & 255); }

struct Box {
  std::vector<uint32_t> ids;  // indices into the unique color list
};
}  // namespace

CookedTexture makeClut(const ImageRgba& img, int bits) {
  const size_t n = static_cast<size_t>(img.width) * img.height;
  // Unique colors in first-appearance order.
  std::map<uint32_t, uint32_t> idOf;
  std::vector<uint32_t> colors;
  std::vector<uint64_t> counts;
  std::vector<uint32_t> pixelId(n);
  for (size_t i = 0; i < n; ++i) {
    uint32_t c = packRgba(&img.rgba[i * 4]);
    auto it = idOf.find(c);
    if (it == idOf.end()) {
      it = idOf.emplace(c, static_cast<uint32_t>(colors.size())).first;
      colors.push_back(c);
      counts.push_back(0);
    }
    ++counts[it->second];
    pixelId[i] = it->second;
  }
  if (bits == 0) bits = colors.size() <= 16 ? 4 : 8;
  const size_t limit = bits == 4 ? 16 : 256;

  std::vector<uint32_t> palette;        // up to `limit` entries
  std::vector<uint32_t> paletteOfUnique(colors.size());
  if (colors.size() <= limit) {
    palette = colors;
    for (size_t i = 0; i < colors.size(); ++i) paletteOfUnique[i] = static_cast<uint32_t>(i);
  } else {
    // Median cut: split the box with the widest channel range at the weighted median.
    std::vector<Box> boxes(1);
    for (uint32_t i = 0; i < colors.size(); ++i) boxes[0].ids.push_back(i);
    auto rangeOf = [&](const Box& b, int& axis) {
      int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
      for (uint32_t id : b.ids)
        for (int k = 0; k < 4; ++k) {
          lo[k] = std::min(lo[k], chan(colors[id], k));
          hi[k] = std::max(hi[k], chan(colors[id], k));
        }
      int best = -1;
      axis = 0;
      for (int k = 0; k < 4; ++k)
        if (hi[k] - lo[k] > best) { best = hi[k] - lo[k]; axis = k; }
      return best;
    };
    while (boxes.size() < limit) {
      int bi = -1, bestRange = 0, bestAxis = 0;
      for (size_t i = 0; i < boxes.size(); ++i) {
        if (boxes[i].ids.size() < 2) continue;
        int axis;
        int r = rangeOf(boxes[i], axis);
        if (r > bestRange) { bestRange = r; bi = static_cast<int>(i); bestAxis = axis; }
      }
      if (bi < 0) break;
      Box& b = boxes[static_cast<size_t>(bi)];
      std::sort(b.ids.begin(), b.ids.end(), [&](uint32_t x, uint32_t y) {
        int cx = chan(colors[x], bestAxis), cy = chan(colors[y], bestAxis);
        return cx != cy ? cx < cy : colors[x] < colors[y];
      });
      uint64_t total = 0;
      for (uint32_t id : b.ids) total += counts[id];
      uint64_t acc = 0;
      size_t cut = 1;
      for (size_t i = 0; i < b.ids.size() - 1; ++i) {
        acc += counts[b.ids[i]];
        cut = i + 1;
        if (acc * 2 >= total) break;
      }
      Box nb;
      nb.ids.assign(b.ids.begin() + static_cast<long>(cut), b.ids.end());
      b.ids.resize(cut);
      boxes.push_back(std::move(nb));
    }
    for (const Box& b : boxes) {
      uint64_t sum[4] = {0, 0, 0, 0}, total = 0;
      for (uint32_t id : b.ids) {
        total += counts[id];
        for (int k = 0; k < 4; ++k) sum[k] += static_cast<uint64_t>(chan(colors[id], k)) * counts[id];
      }
      uint32_t c = 0;
      for (int k = 0; k < 4; ++k) c |= static_cast<uint32_t>((sum[k] + total / 2) / total) << (8 * k);
      palette.push_back(c);
    }
    for (size_t i = 0; i < colors.size(); ++i) {
      int64_t bestD = -1;
      uint32_t best = 0;
      for (size_t p = 0; p < palette.size(); ++p) {
        int64_t d = 0;
        for (int k = 0; k < 4; ++k) {
          int64_t e = chan(colors[i], k) - chan(palette[p], k);
          d += e * e;
        }
        if (bestD < 0 || d < bestD) { bestD = d; best = static_cast<uint32_t>(p); }
      }
      paletteOfUnique[i] = best;
    }
  }

  CookedTexture t;
  t.width = img.width;
  t.height = img.height;
  t.palette = palette;
  t.palette.resize(limit, 0);
  if (bits == 8) {
    t.format = 2;
    t.pixels.resize(n);
    for (size_t i = 0; i < n; ++i) t.pixels[i] = static_cast<uint8_t>(paletteOfUnique[pixelId[i]]);
  } else {
    t.format = 1;
    size_t stride = (img.width + 1) / 2;
    t.pixels.assign(stride * img.height, 0);
    for (uint32_t y = 0; y < img.height; ++y)
      for (uint32_t x = 0; x < img.width; ++x) {
        uint8_t idx = static_cast<uint8_t>(paletteOfUnique[pixelId[static_cast<size_t>(y) * img.width + x]]);
        t.pixels[y * stride + x / 2] |= static_cast<uint8_t>((x & 1) ? idx << 4 : idx);
      }
  }
  return t;
}

bool ICookerFormatPlugin::cookTexture(const ImageRgba& image, const TextureImport& settings, CookedTexture& out, std::string& error) {
  if (settings.format != "auto" && settings.format != "rgba8") {
    error = "texture format '" + settings.format + "' is not available on target '" + std::string(id()) + "' (RGBA8 only)";
    return false;
  }
  out = makeRgba8(image);
  return true;
}

bool ICookerFormatPlugin::cookAudio(PcmAudio& pcm, const AudioImport& s, std::string&) {
  if (s.mono) audioDownmixMono(pcm);
  if (s.halveRate) audioHalveRate(pcm);
  if (s.sampleRate) audioResample(pcm, s.sampleRate);
  return true;
}

// ---- glTF ---------------------------------------------------------------------------------------
bool importGltf(const std::filesystem::path& file, PrimitiveMesh& out, std::string& error) {
  cgltf_options opt = {};
  cgltf_data* data = nullptr;
  std::string path = file.string();
  if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) {
    error = "glTF parse failed";
    return false;
  }
  struct Guard { cgltf_data* d; ~Guard() { cgltf_free(d); } } guard{data};
  if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) {
    error = "glTF buffers could not be loaded";
    return false;
  }
  for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
    const cgltf_node& node = data->nodes[ni];
    if (!node.mesh) continue;
    float m[16];
    cgltf_node_transform_world(&node, m);  // column-major
    auto xf = [&](const float* p) {
      return Vector3(m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
                     m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
    };
    auto xn = [&](const float* p) {
      return Vector3(m[0] * p[0] + m[4] * p[1] + m[8] * p[2], m[1] * p[0] + m[5] * p[1] + m[9] * p[2],
                     m[2] * p[0] + m[6] * p[1] + m[10] * p[2]).normalized();
    };
    for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
      const cgltf_primitive& prim = node.mesh->primitives[pi];
      if (prim.type != cgltf_primitive_type_triangles) {
        error = "only triangle-list primitives are supported";
        return false;
      }
      const cgltf_accessor *pos = nullptr, *nrm = nullptr, *col = nullptr;
      for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
        const cgltf_attribute& a = prim.attributes[ai];
        if (a.type == cgltf_attribute_type_position && a.index == 0) pos = a.data;
        else if (a.type == cgltf_attribute_type_normal && a.index == 0) nrm = a.data;
        else if (a.type == cgltf_attribute_type_color && a.index == 0) col = a.data;
      }
      if (!pos) {
        error = "primitive without POSITION";
        return false;
      }
      float base[4] = {1, 1, 1, 1};
      if (prim.material && prim.material->has_pbr_metallic_roughness)
        memcpy(base, prim.material->pbr_metallic_roughness.base_color_factor, sizeof base);
      cgltf_size count = prim.indices ? prim.indices->count : pos->count;
      if (count % 3) {
        error = "index count is not a multiple of 3";
        return false;
      }
      auto vertexIndex = [&](cgltf_size i) { return prim.indices ? cgltf_accessor_read_index(prim.indices, i) : i; };
      for (cgltf_size t = 0; t < count; t += 3) {
        Vector3 p[3], n[3];
        Color32 c[3];
        for (int k = 0; k < 3; ++k) {
          cgltf_size vi = vertexIndex(t + static_cast<cgltf_size>(k));
          float v[4] = {0, 0, 0, 1};
          cgltf_accessor_read_float(pos, vi, v, 3);
          p[k] = xf(v);
          if (nrm) {
            float nv[3] = {0, 1, 0};
            cgltf_accessor_read_float(nrm, vi, nv, 3);
            n[k] = xn(nv);
          }
          float cv[4] = {base[0], base[1], base[2], base[3]};
          if (col) {
            float rv[4] = {1, 1, 1, 1};
            cgltf_accessor_read_float(col, vi, rv, 4);
            for (int q = 0; q < 4; ++q) cv[q] = base[q] * rv[q];
          }
          auto u8c = [](float f) { return static_cast<uint8_t>(f <= 0 ? 0 : (f >= 1 ? 255 : f * 255.0f + 0.5f)); };
          c[k] = Color32(u8c(cv[0]), u8c(cv[1]), u8c(cv[2]), u8c(cv[3]));
        }
        if (!nrm) {
          Vector3 fn = Vector3::Cross(p[1] - p[0], p[2] - p[0]).normalized();
          n[0] = n[1] = n[2] = fn;
        }
        for (int k = 0; k < 3; ++k) {
          out.positions.push_back(p[k]);
          out.normals.push_back(n[k]);
          out.colors.push_back(c[k]);
        }
      }
    }
  }
  if (out.positions.empty()) {
    error = "glTF holds no triangle meshes";
    return false;
  }
  return true;
}


// ---- F5: texture helpers ----------------------------------------------------------------------
ImageRgba boxDownscale(const ImageRgba& img, uint32_t w, uint32_t h) {
  ImageRgba out;
  out.width = w = std::max(1u, w);
  out.height = h = std::max(1u, h);
  out.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  if (img.width == 0 || img.height == 0) return out;
  for (uint32_t y = 0; y < h; ++y) {
    // Source rows [y0, y1) covered by output row y (at least one row), in 16.16 fixed point so the result is exact and stable.
    const uint64_t y0 = static_cast<uint64_t>(y) * img.height * 65536 / h, y1 = std::max(y0 + 1, static_cast<uint64_t>(y + 1) * img.height * 65536 / h);
    for (uint32_t x = 0; x < w; ++x) {
      const uint64_t x0 = static_cast<uint64_t>(x) * img.width * 65536 / w, x1 = std::max(x0 + 1, static_cast<uint64_t>(x + 1) * img.width * 65536 / w);
      uint64_t acc[4] = {0, 0, 0, 0}, total = 0;
      for (uint64_t sy = y0 >> 16; sy <= (y1 - 1) >> 16 && sy < img.height; ++sy) {
        const uint64_t ya = std::max(y0, sy << 16), yb = std::min(y1, (sy + 1) << 16);
        for (uint64_t sx = x0 >> 16; sx <= (x1 - 1) >> 16 && sx < img.width; ++sx) {
          const uint64_t xa = std::max(x0, sx << 16), xb = std::min(x1, (sx + 1) << 16);
          const uint64_t wgt = (yb - ya) * (xb - xa);
          total += wgt;
          const uint8_t* p = &img.rgba[(sy * img.width + sx) * 4];
          for (int c = 0; c < 4; ++c) acc[c] += wgt * p[c];
        }
      }
      uint8_t* o = &out.rgba[(static_cast<size_t>(y) * w + x) * 4];
      for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>(total ? (acc[c] + total / 2) / total : 0);
    }
  }
  return out;
}

bool pngSize(const std::vector<uint8_t>& b, uint32_t& w, uint32_t& h) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (b.size() < 24 || memcmp(b.data(), sig, 8) != 0 || memcmp(b.data() + 12, "IHDR", 4) != 0) return false;
  auto be = [&](size_t o) { return (static_cast<uint32_t>(b[o]) << 24) | (static_cast<uint32_t>(b[o + 1]) << 16) | (static_cast<uint32_t>(b[o + 2]) << 8) | b[o + 3]; };
  w = be(16);
  h = be(20);
  return true;
}

// ---- F5: glTF model import (normals, UVs, skins, clips, materials) ---------------------------------
Guid deriveGuid(const Guid& g, uint32_t salt) {
  Guid o = g;
  for (int i = 0; i < 4; ++i) o.bytes[12 + i] ^= static_cast<u8>((salt >> (24 - 8 * i)) & 255);
  return o;
}

namespace {

void decomposeMatrix(const float m[16], float t[3], float r[4], float s[3]) {
  t[0] = m[12]; t[1] = m[13]; t[2] = m[14];
  float c[3][3];
  for (int k = 0; k < 3; ++k) {
    c[k][0] = m[k * 4]; c[k][1] = m[k * 4 + 1]; c[k][2] = m[k * 4 + 2];
    s[k] = std::sqrt(c[k][0] * c[k][0] + c[k][1] * c[k][1] + c[k][2] * c[k][2]);
  }
  const float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[1][0] * (c[0][1] * c[2][2] - c[0][2] * c[2][1]) +
                    c[2][0] * (c[0][1] * c[1][2] - c[0][2] * c[1][1]);
  if (det < 0) s[0] = -s[0];
  float R[3][3];  // R[col][row]
  for (int k = 0; k < 3; ++k)
    for (int q = 0; q < 3; ++q) R[k][q] = s[k] != 0 ? c[k][q] / s[k] : (k == q ? 1.0f : 0.0f);
  const float tr = R[0][0] + R[1][1] + R[2][2];
  float x, y, z, w;
  if (tr > 0) {
    float S = std::sqrt(tr + 1.0f) * 2;
    w = 0.25f * S; x = (R[1][2] - R[2][1]) / S; y = (R[2][0] - R[0][2]) / S; z = (R[0][1] - R[1][0]) / S;
  } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
    float S = std::sqrt(1.0f + R[0][0] - R[1][1] - R[2][2]) * 2;
    w = (R[1][2] - R[2][1]) / S; x = 0.25f * S; y = (R[1][0] + R[0][1]) / S; z = (R[2][0] + R[0][2]) / S;
  } else if (R[1][1] > R[2][2]) {
    float S = std::sqrt(1.0f + R[1][1] - R[0][0] - R[2][2]) * 2;
    w = (R[2][0] - R[0][2]) / S; x = (R[1][0] + R[0][1]) / S; y = 0.25f * S; z = (R[2][1] + R[1][2]) / S;
  } else {
    float S = std::sqrt(1.0f + R[2][2] - R[0][0] - R[1][1]) * 2;
    w = (R[0][1] - R[1][0]) / S; x = (R[2][0] + R[0][2]) / S; y = (R[2][1] + R[1][2]) / S; z = 0.25f * S;
  }
  r[0] = x; r[1] = y; r[2] = z; r[3] = w;
}

void nodeLocalTrs(const cgltf_node& n, float t[3], float r[4], float s[3]) {
  if (n.has_matrix) {
    decomposeMatrix(n.matrix, t, r, s);
    return;
  }
  const float dt[3] = {0, 0, 0}, dr[4] = {0, 0, 0, 1}, ds[3] = {1, 1, 1};
  memcpy(t, n.has_translation ? n.translation : dt, sizeof dt);
  memcpy(r, n.has_rotation ? n.rotation : dr, sizeof dr);
  memcpy(s, n.has_scale ? n.scale : ds, sizeof ds);
}

struct Track {
  std::vector<float> times, values;
  int comps = 3;
  bool step = false;
  bool empty() const { return times.empty(); }
};

float quatDot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }

// Value at time `tm` into out[comps]; rotation tracks use shortest-path slerp.
void sampleTrack(const Track& tr, float tm, float* out, bool rot) {
  const size_t n = tr.times.size();
  auto val = [&](size_t i) { return &tr.values[i * static_cast<size_t>(tr.comps)]; };
  if (n == 1 || tm <= tr.times[0]) { memcpy(out, val(0), sizeof(float) * static_cast<size_t>(tr.comps)); return; }
  if (tm >= tr.times[n - 1]) { memcpy(out, val(n - 1), sizeof(float) * static_cast<size_t>(tr.comps)); return; }
  size_t i = 0;
  while (i + 2 < n && tr.times[i + 1] <= tm) ++i;
  const float t0 = tr.times[i], t1 = tr.times[i + 1];
  float k = t1 > t0 ? (tm - t0) / (t1 - t0) : 0.0f;
  if (tr.step) k = 0;
  const float *a = val(i), *b = val(i + 1);
  if (!rot) {
    for (int c = 0; c < tr.comps; ++c) out[c] = a[c] + (b[c] - a[c]) * k;
    return;
  }
  float bb[4] = {b[0], b[1], b[2], b[3]};
  float d = quatDot(a, bb);
  if (d < 0) { for (float& v : bb) v = -v; d = -d; }
  float ka, kb;
  if (d > 0.9995f) { ka = 1 - k; kb = k; }
  else {
    const float th = std::acos(d), sn = std::sin(th);
    ka = std::sin((1 - k) * th) / sn;
    kb = std::sin(k * th) / sn;
  }
  float q[4];
  float len = 0;
  for (int c = 0; c < 4; ++c) { q[c] = a[c] * ka + bb[c] * kb; len += q[c] * q[c]; }
  len = std::sqrt(len);
  for (int c = 0; c < 4; ++c) out[c] = len > 0 ? q[c] / len : (c == 3 ? 1.0f : 0.0f);
}

std::string decodeUri(std::string uri) {
  std::vector<char> buf(uri.begin(), uri.end());
  buf.push_back(0);
  cgltf_decode_uri(buf.data());
  return std::string(buf.data());
}

}  // namespace

int findClipByName(const GltfModel& g, const std::string& name) {
  for (size_t i = 0; i < g.clips.size(); ++i)
    if (g.clips[i].name == name) return static_cast<int>(i);
  return -1;
}

bool importGltfModel(const std::filesystem::path& file, const ModelImport& settings, bool wantSkin, uint32_t clipFps, GltfModel& out,
                     std::string& error) {
  cgltf_options opt = {};
  cgltf_data* data = nullptr;
  const std::string path = file.string();
  if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) {
    error = "glTF parse failed";
    return false;
  }
  struct Guard { cgltf_data* d; ~Guard() { cgltf_free(d); } } guard{data};
  if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) {
    error = "glTF buffers could not be loaded";
    return false;
  }
  out = GltfModel();
  const float scale = settings.scale;

  // Materials (file order).
  for (cgltf_size mi = 0; mi < data->materials_count; ++mi) {
    const cgltf_material& m = data->materials[mi];
    GltfMaterialInfo g;
    g.name = m.name ? m.name : "";
    if (m.has_pbr_metallic_roughness) {
      memcpy(g.color, m.pbr_metallic_roughness.base_color_factor, sizeof g.color);
      const cgltf_texture* tex = m.pbr_metallic_roughness.base_color_texture.texture;
      if (tex && tex->image) {
        if (tex->image->uri && strncmp(tex->image->uri, "data:", 5) != 0)
          g.imagePath = file.parent_path() / decodeUri(tex->image->uri);
        else
          out.warnings.push_back("material '" + g.name + "' has an embedded base color image; add it to the project as a PNG and use a .mat.json");
      }
    }
    g.unlit = m.unlit != 0;
    g.alphaTest = m.alpha_mode == cgltf_alpha_mode_mask;
    out.materials.push_back(g);
  }

  const cgltf_skin* skin = nullptr;
  if (wantSkin)
    for (cgltf_size ni = 0; ni < data->nodes_count && !skin; ++ni)
      if (data->nodes[ni].mesh && data->nodes[ni].skin) skin = data->nodes[ni].skin;
  out.skinned = skin != nullptr;

  // Joints: parents before children, stable in file order.
  std::vector<int> newIndex;  // old skin joint index -> output joint index
  if (skin) {
    const size_t jc = skin->joints_count;
    std::map<const cgltf_node*, size_t> oldOf;
    for (size_t j = 0; j < jc; ++j) oldOf[skin->joints[j]] = j;
    auto parentOld = [&](size_t j) -> int {
      for (const cgltf_node* p = skin->joints[j]->parent; p; p = p->parent) {
        auto it = oldOf.find(p);
        if (it != oldOf.end()) return static_cast<int>(it->second);
      }
      return -1;
    };
    newIndex.assign(jc, -1);
    std::vector<size_t> order;
    while (order.size() < jc) {
      bool progress = false;
      for (size_t j = 0; j < jc; ++j) {
        if (newIndex[j] >= 0) continue;
        const int p = parentOld(j);
        if (p >= 0 && newIndex[static_cast<size_t>(p)] < 0) continue;
        newIndex[j] = static_cast<int>(order.size());
        order.push_back(j);
        progress = true;
      }
      if (!progress) { error = "glTF skin has a joint cycle"; return false; }
    }
    for (size_t k = 0; k < jc; ++k) {
      const size_t j = order[k];
      const cgltf_node& n = *skin->joints[j];
      GltfJoint gj;
      gj.name = n.name ? n.name : ("joint" + std::to_string(k));
      const int p = parentOld(j);
      gj.parent = p < 0 ? -1 : newIndex[static_cast<size_t>(p)];
      if (p < 0 && n.parent) {  // root joint under non-joint nodes: fold their transform into it so model space = glTF space
        float w[16];
        cgltf_node_transform_world(&n, w);
        decomposeMatrix(w, gj.t, gj.r, gj.s);
      } else {
        nodeLocalTrs(n, gj.t, gj.r, gj.s);
      }
      if (skin->inverse_bind_matrices) {
        cgltf_accessor_read_float(skin->inverse_bind_matrices, j, gj.invBind, 16);
      } else {
        for (int q = 0; q < 16; ++q) gj.invBind[q] = (q % 5 == 0) ? 1.0f : 0.0f;
      }
      for (int q = 0; q < 3; ++q) { gj.t[q] *= scale; gj.invBind[12 + q] *= scale; }
      out.joints.push_back(gj);
    }
  }

  // Geometry.
  auto u8c = [](float f) { return static_cast<uint8_t>(f <= 0 ? 0 : (f >= 1 ? 255 : f * 255.0f + 0.5f)); };
  for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
    const cgltf_node& node = data->nodes[ni];
    if (!node.mesh) continue;
    const bool nodeSkinned = skin && node.skin == skin;
    if (skin && !nodeSkinned) {
      out.warnings.push_back("node '" + std::string(node.name ? node.name : "") + "' has a mesh without the skin and is ignored (a skinned glTF imports its skinned meshes only)");
      continue;
    }
    float m[16];
    cgltf_node_transform_world(&node, m);
    auto xf = [&](const float* p) {
      if (nodeSkinned) return Vector3(p[0], p[1], p[2]);
      return Vector3(m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
                     m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
    };
    auto xn = [&](const float* p) {
      if (nodeSkinned) return Vector3(p[0], p[1], p[2]).normalized();
      return Vector3(m[0] * p[0] + m[4] * p[1] + m[8] * p[2], m[1] * p[0] + m[5] * p[1] + m[9] * p[2],
                     m[2] * p[0] + m[6] * p[1] + m[10] * p[2]).normalized();
    };
    for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
      const cgltf_primitive& prim = node.mesh->primitives[pi];
      if (prim.type != cgltf_primitive_type_triangles) {
        error = "only triangle-list primitives are supported";
        return false;
      }
      const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *col = nullptr, *jn[2] = {nullptr, nullptr}, *wt[2] = {nullptr, nullptr};
      for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
        const cgltf_attribute& a = prim.attributes[ai];
        if (a.type == cgltf_attribute_type_position && a.index == 0) pos = a.data;
        else if (a.type == cgltf_attribute_type_normal && a.index == 0) nrm = a.data;
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
        else if (a.type == cgltf_attribute_type_color && a.index == 0) col = a.data;
        else if (a.type == cgltf_attribute_type_joints && a.index < 2) jn[a.index] = a.data;
        else if (a.type == cgltf_attribute_type_weights && a.index < 2) wt[a.index] = a.data;
      }
      if (!pos) {
        error = "primitive without POSITION";
        return false;
      }
      if (nodeSkinned && (!jn[0] || !wt[0])) {
        error = "skinned primitive without JOINTS_0 / WEIGHTS_0";
        return false;
      }
      const int32_t matIdx = prim.material ? static_cast<int32_t>(prim.material - data->materials) : -1;
      if (out.mesh.positions.empty()) out.meshMaterial = matIdx;
      else if (matIdx != out.meshMaterial) out.warnings.push_back("the glTF uses more than one material; only the first is kept (split it into one object per material)");
      const cgltf_size count = prim.indices ? prim.indices->count : pos->count;
      if (count % 3) {
        error = "index count is not a multiple of 3";
        return false;
      }
      auto vertexIndex = [&](cgltf_size i) { return prim.indices ? cgltf_accessor_read_index(prim.indices, i) : i; };
      for (cgltf_size t = 0; t < count; t += 3) {
        Vector3 p[3], n[3];
        float tc[3][2] = {};
        Color32 c[3];
        SkinInfluences inf[3];
        for (int k = 0; k < 3; ++k) {
          const cgltf_size vi = vertexIndex(t + static_cast<cgltf_size>(k));
          float v[4] = {0, 0, 0, 1};
          cgltf_accessor_read_float(pos, vi, v, 3);
          v[0] *= scale; v[1] *= scale; v[2] *= scale;
          p[k] = xf(v);
          if (nrm) {
            float nv[3] = {0, 1, 0};
            cgltf_accessor_read_float(nrm, vi, nv, 3);
            n[k] = xn(nv);
          }
          if (uv) cgltf_accessor_read_float(uv, vi, tc[k], 2);
          float rv[4] = {1, 1, 1, 1};
          if (col) cgltf_accessor_read_float(col, vi, rv, cgltf_num_components(col->type));
          c[k] = Color32(u8c(rv[0]), u8c(rv[1]), u8c(rv[2]), u8c(rv[3]));
          if (nodeSkinned) {
            SkinInfluences si;
            for (int set = 0; set < 2; ++set) {
              if (!jn[set] || !wt[set]) continue;
              cgltf_uint ji[4] = {0, 0, 0, 0};
              float wv[4] = {0, 0, 0, 0};
              cgltf_accessor_read_uint(jn[set], vi, ji, 4);
              cgltf_accessor_read_float(wt[set], vi, wv, 4);
              for (int q = 0; q < 4; ++q) {
                if (wv[q] <= 0.0f) continue;
                if (ji[q] >= newIndex.size()) { error = "JOINTS_0 refers to a joint outside the skin"; return false; }
                const uint16_t jo = static_cast<uint16_t>(newIndex[ji[q]]);
                bool merged = false;
                for (uint8_t e = 0; e < si.count; ++e)
                  if (si.joint[e] == jo) { si.weight[e] += wv[q]; merged = true; }
                if (!merged && si.count < 8) { si.joint[si.count] = jo; si.weight[si.count] = wv[q]; ++si.count; }
              }
            }
            inf[k] = si;
          }
        }
        if (!nrm || settings.flatNormals) {
          Vector3 fn = Vector3::Cross(p[1] - p[0], p[2] - p[0]).normalized();
          n[0] = n[1] = n[2] = fn;
        }
        for (int k = 0; k < 3; ++k) {
          out.mesh.positions.push_back(p[k]);
          out.mesh.normals.push_back(n[k]);
          out.mesh.uvs.push_back(tc[k][0]);
          out.mesh.uvs.push_back(tc[k][1]);
          out.mesh.colors.push_back(c[k]);
          if (nodeSkinned) out.influences.push_back(inf[k]);
        }
      }
    }
  }
  if (out.mesh.positions.empty()) {
    error = "glTF holds no triangle meshes";
    return false;
  }

  // Clips: resample every joint to integer fps.
  if (skin) {
    const uint32_t fps = clipFps ? clipFps : 30;
    std::map<const cgltf_node*, int> jointOf;
    for (size_t j = 0; j < skin->joints_count; ++j) jointOf[skin->joints[j]] = newIndex[j];
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
      const cgltf_animation& an = data->animations[ai];
      const size_t jc = out.joints.size();
      std::vector<Track> tTrack(jc), rTrack(jc), sTrack(jc);
      float duration = 0;
      for (cgltf_size ci = 0; ci < an.channels_count; ++ci) {
        const cgltf_animation_channel& ch = an.channels[ci];
        auto it = jointOf.find(ch.target_node);
        if (it == jointOf.end() || ch.target_path == cgltf_animation_path_type_weights || ch.target_path == cgltf_animation_path_type_invalid) {
          out.warnings.push_back("animation '" + std::string(an.name ? an.name : "") + "' has a channel that is not a joint translation, rotation or scale; ignored");
          continue;
        }
        const cgltf_animation_sampler& sm = *ch.sampler;
        Track tr;
        tr.step = sm.interpolation == cgltf_interpolation_type_step;
        const bool cubic = sm.interpolation == cgltf_interpolation_type_cubic_spline;
        if (cubic) out.warnings.push_back("animation '" + std::string(an.name ? an.name : "") + "' uses CUBICSPLINE; sampled as LINEAR on the key values");
        const bool rot = ch.target_path == cgltf_animation_path_type_rotation;
        tr.comps = rot ? 4 : 3;
        for (cgltf_size k = 0; k < sm.input->count; ++k) {
          float tm = 0;
          cgltf_accessor_read_float(sm.input, k, &tm, 1);
          tr.times.push_back(tm);
          float v[4] = {0, 0, 0, 1};
          cgltf_accessor_read_float(sm.output, cubic ? k * 3 + 1 : k, v, static_cast<cgltf_size>(tr.comps));
          for (int q = 0; q < tr.comps; ++q) tr.values.push_back(v[q]);
        }
        if (tr.empty()) continue;
        duration = std::max(duration, tr.times.back());
        const size_t j = static_cast<size_t>(it->second);
        (rot ? rTrack : ch.target_path == cgltf_animation_path_type_translation ? tTrack : sTrack)[j] = std::move(tr);
      }
      GltfClip clip;
      clip.name = an.name ? an.name : ("clip" + std::to_string(ai));
      clip.fps = fps;
      auto poseAt = [&](float tm, PackJointPose* dst) {
        for (size_t j = 0; j < jc; ++j) {
          const GltfJoint& gj = out.joints[j];
          float t[3] = {gj.t[0], gj.t[1], gj.t[2]}, r[4] = {gj.r[0], gj.r[1], gj.r[2], gj.r[3]}, s[3] = {gj.s[0], gj.s[1], gj.s[2]};
          if (!tTrack[j].empty()) { sampleTrack(tTrack[j], tm, t, false); for (float& v : t) v *= scale; }
          if (!rTrack[j].empty()) sampleTrack(rTrack[j], tm, r, true);
          if (!sTrack[j].empty()) sampleTrack(sTrack[j], tm, s, false);
          PackJointPose& pp = dst[j];
          memcpy(pp.r, r, sizeof r);
          memcpy(pp.t, t, sizeof t);
          pp.s = (s[0] + s[1] + s[2]) / 3.0f;
        }
      };
      const double exact = static_cast<double>(duration) * fps;
      uint32_t n = static_cast<uint32_t>(std::lround(exact));
      std::vector<PackJointPose> first(jc), last(jc);
      poseAt(0.0f, first.data());
      poseAt(duration, last.data());
      bool same = true;
      for (size_t j = 0; j < jc && same; ++j) {
        for (int q = 0; q < 4; ++q) same = same && std::fabs(first[j].r[q] - last[j].r[q]) < 1e-4f;
        for (int q = 0; q < 3; ++q) same = same && std::fabs(first[j].t[q] - last[j].t[q]) < 1e-4f * std::max(1.0f, scale);
        same = same && std::fabs(first[j].s - last[j].s) < 1e-4f;
      }
      clip.loop = same && n >= 1;
      clip.frameCount = clip.loop ? n : n + 1;  // a looping clip drops the duplicate last frame
      if (clip.frameCount == 0) clip.frameCount = 1;
      clip.poses.resize(static_cast<size_t>(clip.frameCount) * jc);
      for (uint32_t f = 0; f < clip.frameCount; ++f)
        poseAt(std::min(duration, static_cast<float>(f) / static_cast<float>(fps)), &clip.poses[static_cast<size_t>(f) * jc]);
      out.clips.push_back(std::move(clip));
    }
  }
  return true;
}

}  // namespace rensga
