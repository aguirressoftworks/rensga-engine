// MVP-20: PSP cooker format plugin. Meshes become GU vertices {u32 color; float x,y,z}
// (GU_COLOR_8888 | GU_VERTEX_32BITF) with the directional light baked into the vertex colors. A cook for the psp target
// itself then converts them to compact 16-bit vertices (MSHX); F4.05 textures: see psp_format.h.
#include <math.h>
#include <string.h>

#include <algorithm>

#include "rensga/cooker/model3d.h"
#include "rensga/cooker/plugin.h"
#include "rensga/cooker/psp_format.h"
#include "rensga/core/pack_format.h"

namespace rensga {

class PspCookerPlugin final : public ICookerFormatPlugin {
 public:
  std::string_view id() const override { return "psp"; }
  bool cookTexture(const ImageRgba& image, const TextureImport& s, CookedTexture& out, std::string& error) override {
    return pspCookTexture(image, s.format, out, error);
  }
  bool decodeCookedTexture(const PackTexRec& rec, const uint8_t* blob, ImageRgba& out, std::string& error) const override {
    // The PSP forms (swizzle, padding, CLUT4/8, 5551, 4444) are all handled by the shared decoder.
    return ICookerFormatPlugin::decodeCookedTexture(rec, blob, out, error);
  }
  // F5: C16 compact lit / skinned vertices (20 / 28 bytes). Positions are i16 around the bounds center with one power-of-two
  // scale (position = decodeOffset + i16 * decodeScale), normals i16 / 32767, UVs u16 / 32768 (range [0, 2)). A mesh whose UVs
  // leave [0, 2) is written in the float form instead (desc.vertexFormat tells the caller).
  bool cookLitMesh(const LitMesh& m, std::vector<uint8_t>& out, PackMeshLit& desc, std::string& error) override {
    std::vector<uint8_t> floatBytes;
    if (!ICookerFormatPlugin::cookLitMesh(m, floatBytes, desc, error)) return false;
    if (!uvFitsC16(m)) {
      out.insert(out.end(), floatBytes.begin(), floatBytes.end());
      return true;
    }
    const size_t n = m.positions.size();
    Vector3 lo = m.positions[0], hi = m.positions[0];
    for (const Vector3& p : m.positions) {
      lo = Vector3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
      hi = Vector3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    const float off[3] = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    float half = 0;
    const float hv[3] = {hi.x, hi.y, hi.z}, lv[3] = {lo.x, lo.y, lo.z};
    for (int k = 0; k < 3; ++k) half = std::max(half, std::max(hv[k] - off[k], off[k] - lv[k]));
    float scale = 1.0f;
    if (half > 0.0f) {
      int e = 0;
      frexpf(half / 32767.0f, &e);
      scale = ldexpf(1.0f, e);
      if (scale * 32767.0f < half) scale *= 2.0f;
    }
    const bool skinned = m.skinned();
    desc.vertexFormat = static_cast<uint32_t>(skinned ? PackVertexFormat::SkinC16 : PackVertexFormat::LitC16);
    for (int k = 0; k < 3; ++k) desc.decodeOffset[k] = off[k];
    desc.decodeScale = scale;
    const size_t stride = skinned ? sizeof(PackVertexSkinC16) : sizeof(PackVertexLitC16);
    const size_t base = out.size();
    out.resize(base + n * stride);
    auto q = [](float f, float lim) { return static_cast<int>(std::max(-lim, std::min(lim, roundf(f)))); };
    for (size_t i = 0; i < n; ++i) {
      PackVertexLitC16 v = {};
      const float u = m.uvs.empty() ? 0.0f : m.uvs[i * 2], w = m.uvs.empty() ? 0.0f : m.uvs[i * 2 + 1];
      v.uv[0] = static_cast<uint16_t>(std::min(65535.0f, roundf(u * 32768.0f)));
      v.uv[1] = static_cast<uint16_t>(std::min(65535.0f, roundf(w * 32768.0f)));
      const Color32 col = m.colors.empty() ? Color32(255, 255, 255, 255) : m.colors[i];
      v.color = static_cast<uint32_t>(col.r) | (static_cast<uint32_t>(col.g) << 8) | (static_cast<uint32_t>(col.b) << 16) | (static_cast<uint32_t>(col.a) << 24);
      v.n[0] = static_cast<int16_t>(q(m.normals[i].x * 32767.0f, 32767.0f));
      v.n[1] = static_cast<int16_t>(q(m.normals[i].y * 32767.0f, 32767.0f));
      v.n[2] = static_cast<int16_t>(q(m.normals[i].z * 32767.0f, 32767.0f));
      v.p[0] = static_cast<int16_t>(q((m.positions[i].x - off[0]) / scale, 32767.0f));
      v.p[1] = static_cast<int16_t>(q((m.positions[i].y - off[1]) / scale, 32767.0f));
      v.p[2] = static_cast<int16_t>(q((m.positions[i].z - off[2]) / scale, 32767.0f));
      uint8_t* dst = out.data() + base + i * stride;
      if (skinned) {
        memcpy(dst, m.weights[i].data(), 8);
        memcpy(dst + 8, &v, sizeof v);
      } else {
        memcpy(dst, &v, sizeof v);
      }
    }
    while (out.size() % 16) out.push_back(0);
    return true;
  }
  bool hasCompactMeshes() const override { return true; }
  bool compactMeshes(const std::vector<uint8_t>& floatBytes, std::vector<uint8_t>& compactBytes, PackMeshExt& ext,
                     std::string& error) override {
    return pspCompactVertices(floatBytes, compactBytes, ext, error);
  }
  bool cookMesh(const PrimitiveMesh& mesh, const BakedLight& light, Color32 base, std::vector<uint8_t>& out,
                uint32_t& outCount, std::string& error) override {
    if (mesh.positions.size() != mesh.normals.size() || mesh.positions.size() % 3 != 0) {
      error = "mesh is not a triangle list with one normal per vertex";
      return false;
    }
    Vector3 toLight = (-light.direction).normalized();
    Scalar amb = light.ambient < 0 ? 0 : (light.ambient > 1 ? 1 : light.ambient);
    size_t first = out.size();
    out.resize(first + mesh.positions.size() * sizeof(PackVertex));
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
      Scalar nl = Vector3::Dot(mesh.normals[i], toLight);
      if (nl < 0) nl = 0;
      Scalar k = amb + (1.0f - amb) * nl;
      const bool vc = mesh.colors.size() == mesh.positions.size();
      auto ch = [&](uint8_t baseC, uint8_t lightC, uint8_t vertC) {
        Scalar v = static_cast<Scalar>(baseC) * (static_cast<Scalar>(vertC) / 255.0f) * (static_cast<Scalar>(lightC) / 255.0f) * k + 0.5f;
        return static_cast<uint32_t>(v > 255 ? 255 : v);
      };
      PackVertex v;
      const Color32 vcol = vc ? mesh.colors[i] : Color32(255, 255, 255, 255);
      uint32_t alpha = (static_cast<uint32_t>(base.a) * vcol.a + 127) / 255;
      v.color = ch(base.r, light.color.r, vcol.r) | (ch(base.g, light.color.g, vcol.g) << 8) |
                (ch(base.b, light.color.b, vcol.b) << 16) | (alpha << 24);
      v.x = mesh.positions[i].x; v.y = mesh.positions[i].y; v.z = mesh.positions[i].z;
      memcpy(out.data() + first + i * sizeof(PackVertex), &v, sizeof(v));
    }
    outCount = static_cast<uint32_t>(mesh.positions.size());
    return true;
  }
};
RENSGA_REGISTER_COOKER_PLUGIN(PspCookerPlugin);

}  // namespace rensga
