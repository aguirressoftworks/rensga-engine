// MVP-20: per-console cooker format plugins. HOST ONLY. The shared cooker core loads scenes through
// reflection; a plugin only turns generic data into the console's binary form.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rensga/core/math.h"
#include "rensga/cooker/assets.h"
#include "rensga/core/pack_format.h"

namespace rensga {

// Generic triangle-list mesh from the primitive generator (flat-shaded: 3 vertices per triangle).
struct PrimitiveMesh {
  std::vector<Vector3> positions;  // size = 3 * triangleCount
  std::vector<Vector3> normals;    // same size as positions
  std::vector<Color32> colors;     // optional (glTF): per-vertex color, same size as positions; empty = none
  size_t triangleCount() const { return positions.size() / 3; }
};

// Light baked into vertex colors by the cooker (the runtime does no lighting).
struct BakedLight {
  Vector3 direction = Vector3(-0.4f, -1.0f, -0.3f);  // direction the light travels
  Color32 color = Color32(255, 255, 255, 255);
  Scalar ambient = 0.35f;  // 0..1
};

// F5: input of a lit or skinned mesh (glTF import, after the skin split). One triangle list, 3 vertices per triangle.
struct LitMesh {
  std::vector<Vector3> positions;
  std::vector<Vector3> normals;                // same size as positions
  std::vector<float> uvs;                      // 2 floats per vertex (u, v; v down as stored in the image); empty = all zero
  std::vector<Color32> colors;                 // optional vertex colors; empty = white
  std::vector<std::array<uint8_t, 8>> weights; // skinned meshes: 8-bit weights per vertex, sum 128; slot i = the batch's bone i
  bool skinned() const { return !weights.empty(); }
};

class ICookerFormatPlugin {
 public:
  virtual ~ICookerFormatPlugin() = default;
  virtual std::string_view id() const = 0;  // target id, e.g. "psp"
  // Cooks one mesh variant (already transformed to the object's local space) into the target's
  // vertex bytes, applying `baseColor` and `light`. Appends to outVertexBytes (16-byte multiples).
  virtual bool cookMesh(const PrimitiveMesh& mesh, const BakedLight& light, Color32 baseColor,
                        std::vector<uint8_t>& outVertexBytes, uint32_t& outVertexCount,
                        std::string& error) {
    (void)mesh; (void)light; (void)baseColor; (void)outVertexBytes; (void)outVertexCount;
    error = "target '" + std::string(id()) + "' has no mesh format yet";
    return false;
  }
  // Compact mesh form (F4.05, MSHX). Only a target's own cook uses it: cook.cpp calls these when the cooked target IS this
  // plugin's profile, never for a platform cook (linux, windows, android) of the same project, which keeps float vertices.
  virtual bool hasCompactMeshes() const { return false; }
  // Converts the float PackVertex bytes cookMesh produced into the compact records plus the one PackMeshExt for the pack.
  // Returns false (leaving the float form) when there is nothing to compact; `error` is set only for a real failure.
  virtual bool compactMeshes(const std::vector<uint8_t>& floatVertexBytes, std::vector<uint8_t>& compactBytes,
                             PackMeshExt& ext, std::string& error) {
    (void)floatVertexBytes; (void)compactBytes; (void)ext; (void)error;
    return false;
  }
  // F5: lit / skinned mesh vertex bytes for VTX3. The default writes the float forms (PackVertexLit / PackVertexSkin) and
  // fills desc.vertexFormat (LitF or SkinF), desc.vertexCount and the bounds sphere; the caller sets material, skin, batches,
  // flags and vertexByteOffset. A target plugin overrides it to write 16-bit forms (and sets decodeOffset / decodeScale).
  virtual bool cookLitMesh(const LitMesh& mesh, std::vector<uint8_t>& outVertexBytes, PackMeshLit& desc, std::string& error);
  // F5: decodes a cooked texture back to RGBA8 for the editor's preview (the form the target's renderer would sample:
  // unswizzled, palette applied, 16-bit formats expanded). `blob` is the pack's BLOB section. The default handles every PackTexFormat
  // and the swizzle flag; a plugin overrides it only for a format of its own. Output is the SOURCE size (width x height).
  virtual bool decodeCookedTexture(const PackTexRec& rec, const uint8_t* blob, ImageRgba& out, std::string& error) const;
  // false for platform targets (linux, windows, android): they share the project's console profile
  // for meshes and only choose texture/audio forms. The mesh plugin is then found by profile id.
  virtual bool isProfile() const { return true; }
  // Textures: default is RGBA8 pass-through (PC targets, PSP until its own formats land).
  virtual bool cookTexture(const ImageRgba& image, const TextureImport& settings, CookedTexture& out, std::string& error);
  // Audio: default applies the generic import settings (mono, halveRate) and keeps PCM16.
  virtual bool cookAudio(PcmAudio& pcm, const AudioImport& settings, std::string& error);
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
