// MVP-20: PSP cooker format plugin. Meshes become GU vertices {u32 color; float x,y,z}
// (GU_COLOR_8888 | GU_VERTEX_32BITF) with the directional light baked into the vertex colors.
#include <math.h>
#include <string.h>

#include "rensga/core/pack_format.h"
#include "rensga/cooker/plugin.h"

namespace rensga {

class PspCookerPlugin final : public ICookerFormatPlugin {
 public:
  std::string_view id() const override { return "psp"; }
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
      auto ch = [&](uint8_t baseC, uint8_t lightC) {
        Scalar v = static_cast<Scalar>(baseC) * (static_cast<Scalar>(lightC) / 255.0f) * k + 0.5f;
        return static_cast<uint32_t>(v > 255 ? 255 : v);
      };
      PackVertex v;
      v.color = ch(base.r, light.color.r) | (ch(base.g, light.color.g) << 8) | (ch(base.b, light.color.b) << 16) |
                (static_cast<uint32_t>(base.a) << 24);
      v.x = mesh.positions[i].x; v.y = mesh.positions[i].y; v.z = mesh.positions[i].z;
      memcpy(out.data() + first + i * sizeof(PackVertex), &v, sizeof(v));
    }
    outCount = static_cast<uint32_t>(mesh.positions.size());
    return true;
  }
};
RENSGA_REGISTER_COOKER_PLUGIN(PspCookerPlugin);

}  // namespace rensga
