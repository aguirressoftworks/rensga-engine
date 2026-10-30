// F2.03: importers (PNG, WAV, glTF) and shared texture/audio helpers.
#include "rensga/cooker/assets.h"

#include <string.h>

#include <algorithm>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

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

}  // namespace rensga
