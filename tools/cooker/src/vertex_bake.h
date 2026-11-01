// F6.02 (B3, Editor 8): baked vertex lighting. At cook time the lights of the scene and a cheap ambient occlusion term are
// evaluated per vertex and written into the vertex colors of the cooked mesh (PSP and PS2 draw them as is, PC draws the same).
//
//   color = clamp(ambient * ao + sum over lights of  color * intensity * shadow * atten * max(0, n . l))
//
// Lights: DirectionalLight (direction, color) and the F5 Light (type 0 = directional along the object's +Z axis, 1 = point with
// the runtime falloff intensity * clamp01(1 - d / range)^2). Shadows and AO are ray casts against every active static
// MeshRenderer of the scene (triangle lists in world space, an AABB per mesh instance in front of the triangles). The AO samples
// are a fixed cosine-weighted pattern (no random numbers), so a bake is a pure function of the scene: the same input gives
// the same bytes on every host.
//
// Toggled per MeshRenderer by the appended field `bakeLighting` (default false: nothing changes). A baked mesh without a material takes the
// cooker's baked-light path with a neutral light; one with a material is cooked with PackMeshLit.flags kMeshLightBaked, which makes
// renderWorld draw it unlit whatever its material says (the light is in the colors). The renderers see ordinary vertex colors.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/math.h"
#include "rensga/cooker/vertex_bake_stats.h"

namespace rensga {
namespace cooker {

struct BakeLightIn {
  int type = 0;                 // 0 directional, 1 point
  Vector3 pos;                  // point lights
  Vector3 dir = Vector3(0, -1, 0);  // directional: direction the light travels (unit)
  float rgb[3] = {1, 1, 1};     // color x intensity, 0..n
  float range = 10;             // point lights
};

struct BakeSettings {
  float ambient[3] = {0.35f, 0.35f, 0.35f};
  bool shadows = true;
  float aoStrength = 0.65f;     // 0 = no AO, 1 = fully dark in a closed corner
  int aoSamples = 16;           // rays per vertex (1..64)
  float aoDistance = 2.0f;      // world units; farther hits do not occlude
  float bias = 0.03f;           // ray origin offset along the normal
};

class VertexBaker {
 public:
  explicit VertexBaker(const BakeSettings& s = BakeSettings()) : set_(s) {}
  void addLight(const BakeLightIn& l) { lights_.push_back(l); }
  // `world` = 3 vertices per triangle in world space.
  void addOccluder(const std::vector<Vector3>& world);
  size_t occluderCount() const { return occ_.size(); }
  size_t triangleCount() const;
  size_t lightCount() const { return lights_.size(); }
  const BakeSettings& settings() const { return set_; }

  // Bakes `n` vertices (positions in the mesh's local space, `world` its object-to-world matrix). `normals` are local normals,
  // or world normals when `normalsAreWorld` (the cooker's baked path pre-rotates them). Writes one RGB multiplier per vertex
  // (alpha 255) into `out`.
  void bake(const Vector3* pos, const Vector3* normals, size_t n, const Matrix4& world, bool normalsAreWorld, uint8_t* outRgba) const;

  // Statistics of the last bakes (rays cast), for the progress line / tests.
  uint64_t raysCast() const { return rays_; }

 private:
  struct Occ { float bmin[3], bmax[3]; std::vector<Vector3> tris; };  // a chunk of at most kChunkTris consecutive triangles
  static constexpr size_t kChunkTris = 16;
  bool occluded(const Vector3& o, const Vector3& d, float maxT) const;
  BakeSettings set_;
  std::vector<BakeLightIn> lights_;
  std::vector<Occ> occ_;
  mutable uint64_t rays_ = 0;
  uint64_t key_ = 0;  // hash of settings, lights and occluder triangles: part of the bake cache key
 public:
  void finalizeKey();  // after the last addLight / addOccluder
  uint64_t key() const { return key_; }
};

// ---- scene description handed over by cook.cpp (plain data, no cooker internals) ----
struct BakeObjectIn {
  int parent = -1;
  float pos[3] = {0, 0, 0}, rot[4] = {0, 0, 0, 1}, scale[3] = {1, 1, 1};
  bool active = true;
  struct Comp { std::string type; nlohmann::json fields; };
  std::vector<Comp> comps;
};

// Loads the local-space triangle list (positions + normals, 3 vertices per triangle) of a MeshRenderer mesh GUID.
using BakeMeshLoader = std::function<bool(const std::string& meshGuid, std::vector<Vector3>& pos, std::vector<Vector3>& nrm, std::string& error)>;

class VertexBakeScene {
 public:
  // Returns null unless at least one active MeshRenderer has bakeLighting = true. Cook errors (a mesh that does not load) are
  // appended to `errors` and the bake of that mesh is skipped.
  static std::unique_ptr<VertexBakeScene> create(const std::vector<BakeObjectIn>& objs, const BakeMeshLoader& load, std::vector<std::string>& errors);
  static bool wants(const nlohmann::json& meshRendererFields) { return meshRendererFields.value("bakeLighting", false); }
  Matrix4 worldOf(size_t objectIndex) const { return world_[objectIndex]; }
  const VertexBaker& baker() const { return baker_; }
  // Bakes colors for object `i`'s mesh into `colors` (size n, RGBA8). `mult` (n entries, may be empty) is an existing
  // per-vertex color the result is multiplied with (glTF vertex colors).
  void bakeObject(size_t i, const std::vector<Vector3>& pos, const std::vector<Vector3>& nrm, bool normalsAreWorld, std::vector<uint8_t>& colors) const;
  size_t bakedObjects() const { return bakedObjects_; }
  mutable size_t bakedObjects_ = 0;
  uint64_t totalMeshVertices_ = 0;

 private:
  VertexBakeScene() = default;
 public:
  ~VertexBakeScene();
 private:
  std::unique_lock<std::mutex> gate_;  // one scene bakes at a time: a second cook (editor validation, worker) waits and then hits the cache
  std::vector<Matrix4> world_;
  VertexBaker baker_;
};

}  // namespace cooker
}  // namespace rensga
