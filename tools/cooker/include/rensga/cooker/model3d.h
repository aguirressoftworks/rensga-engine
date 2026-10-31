// F5 (shared 3D, cook side). HOST ONLY. glTF skins/clips/materials, the 8-bone batch split, .mat.json, lit mesh helpers.
// Conventions (docs/rensga-engine/pack-format.md, "Lit and skinned meshes"):
//   * glTF is imported as is: no handedness flip (the baked path never flipped either); winding is reported by kMeshWindingCCW.
//   * A glTF's first animation has the glTF's own GUID as its clip GUID (Animation.clip = the glTF asset GUID); animation k > 0
//     has the glTF GUID with its last 4 bytes XORed by k (deriveGuid). A clip is also findable by name (PackClip3::nameStr).
//   * The skin GUID is the glTF GUID. A glTF's own first material becomes a MATL whose GUID is deriveGuid(glTF GUID, 0x80000000).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rensga/cooker/assets.h"
#include "rensga/cooker/plugin.h"
#include "rensga/core/guid.h"
#include "rensga/core/pack_format.h"

namespace rensga {

struct ModelImport;

// ---- glTF import ----
struct SkinInfluences {      // up to 8 (joint, weight) pairs of one vertex; joint = index into GltfModel::joints
  uint8_t count = 0;
  uint16_t joint[8] = {};
  float weight[8] = {};
};
struct GltfJoint {           // parents come before children
  std::string name;
  int32_t parent = -1;
  float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  float invBind[16] = {};
};
struct GltfClip {
  std::string name;
  uint32_t fps = 30, frameCount = 0;
  bool loop = false;
  std::vector<PackJointPose> poses;  // frameCount x joints, frame-major
};
struct GltfMaterialInfo {
  std::string name;
  std::filesystem::path imagePath;   // resolved base color image file, empty = none
  float color[4] = {1, 1, 1, 1};
  bool unlit = false, alphaTest = false;
};
struct GltfModel {
  LitMesh mesh;                              // triangle list in the file's space; skinned: positions in bind pose, node transform ignored
  std::vector<SkinInfluences> influences;    // parallel to mesh.positions when skinned
  bool skinned = false;
  std::vector<GltfJoint> joints;
  std::vector<GltfClip> clips;
  std::vector<GltfMaterialInfo> materials;   // file order
  int32_t meshMaterial = -1;                 // glTF material index of the first primitive, -1 = none
  std::vector<std::string> warnings;         // sentences without the asset name
};
// wantSkin = false imports the geometry as a static mesh (node transforms applied) even when the file has a skin.
bool importGltfModel(const std::filesystem::path& file, const ModelImport& settings, bool wantSkin, uint32_t clipFps, GltfModel& out,
                     std::string& error);
// Same Guid with its last 4 bytes XORed by `salt` (salt 0 = identical).
Guid deriveGuid(const Guid& g, uint32_t salt);
constexpr uint32_t kDerivedMaterialSalt = 0x80000000u;

// ---- skin split (skin_split.cpp) ----
struct SkinSplitResult {
  LitMesh mesh;                          // reordered by batch, weights filled (slot i = batch bones[i], 8-bit, sum 128)
  std::vector<PackBoneBatch> batches;    // firstVertex / vertexCount relative to the mesh
  uint32_t droppedInfluences = 0;        // influences removed because a triangle needed more than maxBones joints
  uint32_t trimmedTriangles = 0;
  uint32_t unweightedVertices = 0;       // vertices with no influence (bound to the first bone of their batch)
};
// Splits `in` (triangle list, 3 vertices per triangle) into batches of at most maxBones (1..8) joints. `warning` receives one
// sentence when influences were dropped (empty otherwise). False only for inconsistent input.
bool splitSkinnedMesh(const LitMesh& in, const std::vector<SkinInfluences>& influences, uint32_t maxBones, SkinSplitResult& out,
                      std::string& warning, std::string& error);
// Rounds 8 float weights (sum ~1) to 8-bit weights summing to exactly 128 (largest remainder, ties to the lower slot).
void quantizeWeights128(const float w[8], uint8_t out[8]);

// ---- materials (material.cpp) ----
struct MaterialDef {
  std::string textureGuid;               // asset GUID text or empty
  uint8_t color[4] = {255, 255, 255, 255};
  bool unlit = false, alphaTest = false;
};
bool parseMaterialJson(const std::string& text, MaterialDef& out, std::string& error);
uint32_t materialFlags(const MaterialDef& m);                  // kMatUnlit | kMatAlphaTest
uint32_t colorAbgr(const uint8_t rgba[4]);

// ---- lit mesh helpers (lit_mesh.cpp) ----
// Primitive -> lit mesh. Primitives have no UVs: box-projected, each face stretched over the mesh bounds ([0,1]).
LitMesh litMeshFromPrimitive(const PrimitiveMesh& p);
LitMesh litMeshFromGltf(const GltfModel& g);
constexpr float kNearClipEdgeLength = 8.0f;  // a triangle edge longer than this (mesh units) sets kMeshNeedsNearClip
// kMeshWindingCCW when every non-degenerate triangle winds counter-clockwise against its normals (right-handed cross product);
// kMeshNeedsNearClip when a triangle edge is longer than kNearClipEdgeLength. `note` receives a sentence for mixed winding.
uint32_t analyzeMeshFlags(const LitMesh& m, std::string& note);
// All UVs in [0, 2): the 16-bit texture coordinate form (value / 32768) holds them.
bool uvFitsC16(const LitMesh& m);
// Joint-name lookup helper for tests: index of the clip named `name`, or -1.
int findClipByName(const GltfModel& g, const std::string& name);

// ---- texture downscale (assets.cpp) ----
// Box filter to exactly w x h (both >= 1); area average per channel (alpha premultiplied weighting is not used: the RGBA
// channels are averaged independently, which is exact for opaque and mask textures).
ImageRgba boxDownscale(const ImageRgba& img, uint32_t w, uint32_t h);
// PNG size without decoding the pixels; false when it is not a PNG.
bool pngSize(const std::vector<uint8_t>& bytes, uint32_t& w, uint32_t& h);

}  // namespace rensga
