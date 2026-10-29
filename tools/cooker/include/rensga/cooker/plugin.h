// MVP-20: per-console cooker format plugins. HOST ONLY. The shared cooker core loads scenes through
// reflection; a plugin only turns generic data into the console's binary form.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rensga/core/math.h"

namespace rensga {

// Generic triangle-list mesh from the primitive generator (flat-shaded: 3 vertices per triangle).
struct PrimitiveMesh {
  std::vector<Vector3> positions;  // size = 3 * triangleCount
  std::vector<Vector3> normals;    // same size as positions
  size_t triangleCount() const { return positions.size() / 3; }
};

// Light baked into vertex colors by the cooker (the runtime does no lighting).
struct BakedLight {
  Vector3 direction = Vector3(-0.4f, -1.0f, -0.3f);  // direction the light travels
  Color32 color = Color32(255, 255, 255, 255);
  Scalar ambient = 0.35f;  // 0..1
};

class ICookerFormatPlugin {
 public:
  virtual ~ICookerFormatPlugin() = default;
  virtual std::string_view id() const = 0;  // target id, e.g. "psp"
  // Cooks one mesh variant (already transformed to the object's local space) into the target's
  // vertex bytes, applying `baseColor` and `light`. Appends to outVertexBytes (16-byte multiples).
  virtual bool cookMesh(const PrimitiveMesh& mesh, const BakedLight& light, Color32 baseColor,
                        std::vector<uint8_t>& outVertexBytes, uint32_t& outVertexCount,
                        std::string& error) = 0;
};

// Registry by target id. Plugins register from a static registrar in their own library.
class CookerPluginRegistry {
 public:
  static CookerPluginRegistry& instance();
  void add(ICookerFormatPlugin* plugin);
  ICookerFormatPlugin* find(std::string_view id) const;
  const std::vector<ICookerFormatPlugin*>& all() const { return plugins_; }

 private:
  std::vector<ICookerFormatPlugin*> plugins_;
};

struct CookerPluginRegistrar {
  explicit CookerPluginRegistrar(ICookerFormatPlugin* p) { CookerPluginRegistry::instance().add(p); }
};
#define RENSGA_REGISTER_COOKER_PLUGIN(Type)                  \
  static Type RENSGA_CAT(g_cooker_plugin_, Type);            \
  static ::rensga::CookerPluginRegistrar RENSGA_CAT(g_cooker_registrar_, Type)(&RENSGA_CAT(g_cooker_plugin_, Type))

}  // namespace rensga
