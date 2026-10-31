#include "rensga/cooker/plugin.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "rensga/cooker/psp_format.h"

namespace rensga {

CookerPluginRegistry& CookerPluginRegistry::instance() {
  static CookerPluginRegistry r;
  return r;
}
void CookerPluginRegistry::add(ICookerFormatPlugin* plugin) { plugins_.push_back(plugin); }
ICookerFormatPlugin* CookerPluginRegistry::find(std::string_view id) const {
  for (ICookerFormatPlugin* p : plugins_)
    if (p->id() == id) return p;
  return nullptr;
}

bool ICookerFormatPlugin::cookLitMesh(const LitMesh& m, std::vector<uint8_t>& out, PackMeshLit& desc, std::string& error) {
  const size_t n = m.positions.size();
  if (n == 0 || n % 3 != 0 || m.normals.size() != n || (!m.uvs.empty() && m.uvs.size() != n * 2) ||
      (!m.colors.empty() && m.colors.size() != n) || (m.skinned() && m.weights.size() != n)) {
    error = "lit mesh has inconsistent vertex arrays (positions, normals, uvs, colors, weights must match and be a triangle list)";
    return false;
  }
  desc = PackMeshLit{};
  desc.vertexFormat = static_cast<uint32_t>(m.skinned() ? PackVertexFormat::SkinF : PackVertexFormat::LitF);
  desc.vertexCount = static_cast<uint32_t>(n);
  desc.material = desc.skin = kInvalidIndex;
  // Bounds sphere: center of the box, radius to the farthest vertex.
  Vector3 lo = m.positions[0], hi = m.positions[0];
  for (const Vector3& p : m.positions) {
    lo = Vector3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
    hi = Vector3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
  }
  const Vector3 c((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
  float r2 = 0;
  for (const Vector3& p : m.positions) {
    const float dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
    r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
  }
  desc.boundsCenter[0] = c.x; desc.boundsCenter[1] = c.y; desc.boundsCenter[2] = c.z;
  desc.boundsRadius = std::sqrt(r2);
  const size_t stride = m.skinned() ? sizeof(PackVertexSkin) : sizeof(PackVertexLit);
  const size_t base = out.size();
  out.resize(base + n * stride);
  for (size_t i = 0; i < n; ++i) {
    PackVertexLit v = {};
    v.u = m.uvs.empty() ? 0.0f : m.uvs[i * 2];
    v.v = m.uvs.empty() ? 0.0f : m.uvs[i * 2 + 1];
    const Color32 col = m.colors.empty() ? Color32(255, 255, 255, 255) : m.colors[i];
    v.color = static_cast<uint32_t>(col.r) | (static_cast<uint32_t>(col.g) << 8) | (static_cast<uint32_t>(col.b) << 16) | (static_cast<uint32_t>(col.a) << 24);
    v.n[0] = m.normals[i].x; v.n[1] = m.normals[i].y; v.n[2] = m.normals[i].z;
    v.p[0] = m.positions[i].x; v.p[1] = m.positions[i].y; v.p[2] = m.positions[i].z;
    uint8_t* dst = out.data() + base + i * stride;
    if (m.skinned()) {
      std::memcpy(dst, m.weights[i].data(), 8);
      std::memcpy(dst + 8, &v, sizeof v);
    } else {
      std::memcpy(dst, &v, sizeof v);
    }
  }
  // Every mesh in VTX3 starts on a 16-byte boundary: pad the tail.
  while (out.size() % 16) out.push_back(0);
  return true;
}

bool ICookerFormatPlugin::decodeCookedTexture(const PackTexRec& t, const uint8_t* blob, ImageRgba& out, std::string& error) const {
  const uint32_t w = t.width, h = t.height, sw = t.storedW(), sh = t.storedH();
  if (!w || !h || sw < w || sh < h || t.format > kPackTexFormatMax) {
    error = "texture record has a bad size or format";
    return false;
  }
  const uint32_t bits = t.format == 0 ? 32 : t.format == 1 ? 4 : t.format == 2 ? 8 : 16;
  const uint32_t pitch = (sw * bits + 7) / 8;
  if (static_cast<uint64_t>(pitch) * sh > t.pixelBytes) {
    error = "texture pixel data is smaller than its stored size";
    return false;
  }
  std::vector<uint8_t> lin;
  const uint8_t* px = blob + t.pixelOffset;
  if (t.flags & kTexSwizzled) {
    if (pitch % kPspSwizzleBlockBytes || sh % kPspSwizzleBlockRows) {
      error = "swizzled texture has a stored size that is not a multiple of the swizzle block";
      return false;
    }
    lin.resize(static_cast<size_t>(pitch) * sh);
    pspUnswizzle(px, pitch, sh, lin.data());
    px = lin.data();
  }
  out.width = w;
  out.height = h;
  out.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  const uint32_t* pal = t.paletteCount ? reinterpret_cast<const uint32_t*>(blob + t.paletteOffset) : nullptr;
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* row = px + static_cast<size_t>(y) * pitch;
    for (uint32_t x = 0; x < w; ++x) {
      uint8_t* d = &out.rgba[(static_cast<size_t>(y) * w + x) * 4];
      switch (t.format) {
        case 0: std::memcpy(d, row + x * 4, 4); break;
        case 1:
        case 2: {
          const uint32_t idx = t.format == 2 ? row[x] : ((row[x >> 1] >> ((x & 1) * 4)) & 15);
          const uint32_t c = pal && idx < t.paletteCount ? pal[idx] : 0;
          d[0] = c & 255; d[1] = (c >> 8) & 255; d[2] = (c >> 16) & 255; d[3] = (c >> 24) & 255;
          break;
        }
        case 3: {
          const uint32_t v = row[x * 2] | (row[x * 2 + 1] << 8);
          d[0] = static_cast<uint8_t>(((v & 31) * 255 + 15) / 31);
          d[1] = static_cast<uint8_t>((((v >> 5) & 31) * 255 + 15) / 31);
          d[2] = static_cast<uint8_t>((((v >> 10) & 31) * 255 + 15) / 31);
          d[3] = (v & 0x8000) ? 255 : 0;
          break;
        }
        default: {  // RGBA4444
          const uint32_t v = row[x * 2] | (row[x * 2 + 1] << 8);
          d[0] = static_cast<uint8_t>((v & 15) * 17);
          d[1] = static_cast<uint8_t>(((v >> 4) & 15) * 17);
          d[2] = static_cast<uint8_t>(((v >> 8) & 15) * 17);
          d[3] = static_cast<uint8_t>(((v >> 12) & 15) * 17);
          break;
        }
      }
    }
  }
  return true;
}

}  // namespace rensga
