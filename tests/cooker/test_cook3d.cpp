// F5 (shared 3D, cook side): glTF skins and clips, the 8-bone batch split, C16 vertices, materials, texture downscale,
// decodeCookedTexture, and the psp3d_room sample cooking for psp and linux.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga/cooker/model3d.h"
#include "rensga/cooker/plugin.h"
#include "rensga/cooker/psp_format.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

namespace {

fs::path walkGltf() { return fs::path(RENSGA_FIXTURES_DIR) / "gltf" / "blocky_walk.gltf"; }

std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Loaded {
  Arena arena;
  PackView view;
  std::vector<uint8_t> bytes;
  bool open(const fs::path& p) {
    bytes = readAll(p);
    arena.create(bytes.size() + 64);
    u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
    memcpy(mem, bytes.data(), bytes.size());
    return view.open(mem, bytes.size(), p.string().c_str());  // runs validateAssets
  }
};

struct CookOut {
  CookResult result;
  std::vector<CookMessage> warnings;
  std::string firstError;
};

CookOut cookRoom(const char* target, const fs::path& outDir) {
  CookOut o;
  CookOptions opt;
  opt.project = RENSGA_ROOM_DIR;
  opt.target = target;
  opt.force = true;
  opt.outDir = outDir;
  o.result = cookProject(opt, [&](const CookMessage& m) {
    if (m.severity == CookMessage::Warning) o.warnings.push_back(m);
    if (m.severity == CookMessage::Error) {
      fprintf(stderr, "    [cook error] %s\n", m.text.c_str());
      if (o.firstError.empty()) o.firstError = m.text;
    }
  });
  return o;
}

fs::path tempDir(const char* name) {
  fs::path d = fs::temp_directory_path() / (std::string("rensga_cook3d_") + name);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}

ICookerFormatPlugin* plugin(const char* id) { return CookerPluginRegistry::instance().find(id); }

GltfModel importWalk(bool wantSkin = true) {
  GltfModel g;
  ModelImport mi;
  std::string err;
  const bool ok = importGltfModel(walkGltf(), mi, wantSkin, 30, g, err);
  if (!ok) fprintf(stderr, "    [import error] %s\n", err.c_str());
  CHECK(ok);
  return g;
}

}  // namespace

TEST(cook3d_gltf_skin_import_counts) {
  GltfModel g = importWalk();
  CHECK(g.skinned);
  CHECK_EQ(g.joints.size(), size_t(11));
  CHECK_EQ(g.mesh.positions.size(), size_t(156 * 3));
  CHECK_EQ(g.mesh.normals.size(), g.mesh.positions.size());
  CHECK_EQ(g.mesh.uvs.size(), g.mesh.positions.size() * 2);
  CHECK_EQ(g.influences.size(), g.mesh.positions.size());
  CHECK_EQ(g.clips.size(), size_t(2));
  CHECK_EQ(findClipByName(g, "walk"), 0);
  CHECK_EQ(findClipByName(g, "idle"), 1);
  CHECK(g.joints[0].parent == -1);
  for (size_t j = 1; j < g.joints.size(); ++j) CHECK(g.joints[j].parent >= 0 && g.joints[j].parent < static_cast<int>(j));
  for (const SkinInfluences& si : g.influences) {
    float sum = 0;
    CHECK(si.count >= 1 && si.count <= 2);
    for (uint8_t e = 0; e < si.count; ++e) {
      sum += si.weight[e];
      CHECK(si.joint[e] < g.joints.size());
    }
    CHECK_NEAR(sum, 1.0f, 1e-3f);
  }
  // Unit normals and UVs inside the image.
  for (const Vector3& n : g.mesh.normals) CHECK_NEAR(n.magnitude(), 1.0f, 1e-3f);
  for (float u : g.mesh.uvs) CHECK(u >= 0.0f && u <= 1.0f);
  // Inverse bind matrices are rigid (determinant 1 on the 3x3).
  for (const GltfJoint& j : g.joints) {
    const float* m = j.invBind;
    const float det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
    CHECK_NEAR(det, 1.0f, 1e-3f);
  }
  // Material: the glTF's first material is the character texture; asking for a static mesh drops the skin only.
  CHECK(g.meshMaterial >= 0);
  GltfModel st = importWalk(false);
  CHECK(!st.skinned && st.joints.empty() && st.clips.empty());
}

TEST(cook3d_clip_resample) {
  GltfModel g = importWalk();
  const GltfClip& walk = g.clips[0];
  const GltfClip& idle = g.clips[1];
  CHECK_EQ(walk.fps, 30u);
  CHECK(walk.loop);
  CHECK_EQ(walk.frameCount, 30u);  // 1.0 s at 30 fps, the duplicate last key dropped
  CHECK(idle.loop);
  CHECK_EQ(idle.frameCount, 60u);  // 2.0 s
  CHECK_EQ(walk.poses.size(), size_t(walk.frameCount) * g.joints.size());
  // Unit rotations, a smooth path (no pose jumps between frames), and the legs really move.
  float maxStep = 0, knee = 0;
  for (uint32_t f = 0; f < walk.frameCount; ++f)
    for (size_t j = 0; j < g.joints.size(); ++j) {
      const PackJointPose& p = walk.poses[f * g.joints.size() + j];
      CHECK_NEAR(sqrtf(p.r[0] * p.r[0] + p.r[1] * p.r[1] + p.r[2] * p.r[2] + p.r[3] * p.r[3]), 1.0f, 1e-3f);
      const PackJointPose& q = walk.poses[((f + 1) % walk.frameCount) * g.joints.size() + j];
      for (int k = 0; k < 4; ++k) maxStep = fmaxf(maxStep, fabsf(p.r[k] - q.r[k]));
      if (j == 8) knee = fmaxf(knee, fabsf(p.r[0] - walk.poses[j].r[0]));  // LowerLegL
    }
  CHECK(maxStep < 0.2f);
  CHECK(knee > 0.1f);
  // The looped wrap is continuous too (last frame -> frame 0 is a normal step, checked above through the modulo).
}

TEST(cook3d_skin_split_invariants) {
  GltfModel g = importWalk();
  // Force more than one batch with a small cap, and also the PSP cap of 8.
  for (uint32_t cap : {8u, 4u}) {
    SkinSplitResult sr;
    std::string warn, err;
    CHECK(splitSkinnedMesh(g.mesh, g.influences, cap, sr, warn, err));
    CHECK(err.empty());
    const size_t n = g.mesh.positions.size();
    CHECK_EQ(sr.mesh.positions.size(), n);
    CHECK_EQ(sr.mesh.weights.size(), n);
    CHECK(sr.batches.size() >= 2);
    size_t next = 0;
    for (const PackBoneBatch& b : sr.batches) {
      CHECK(b.boneCount >= 1 && b.boneCount <= cap);
      CHECK_EQ(b.firstVertex, static_cast<uint32_t>(next));
      CHECK(b.vertexCount % 3 == 0 && b.vertexCount > 0);
      for (uint32_t q = 0; q < 8; ++q) CHECK((q < b.boneCount) == (b.bones[q] != kBatchBoneUnused));
      for (uint32_t v = b.firstVertex; v < b.firstVertex + b.vertexCount; ++v) {
        int sum = 0;
        for (uint32_t q = 0; q < 8; ++q) {
          sum += sr.mesh.weights[v][q];
          if (q >= b.boneCount) CHECK_EQ(sr.mesh.weights[v][q], 0);  // no weight on an unused slot
        }
        CHECK_EQ(sum, 128);
      }
      next += b.vertexCount;
    }
    CHECK_EQ(next, n);
    CHECK_EQ(sr.droppedInfluences, 0u);
    CHECK(warn.empty());
    // Fidelity: the batch-slot weights reproduce the joint weights of the source triangle (same positions, any order).
    // Every source vertex is found again with its joint weights to within 1/64.
    size_t matched = 0;
    for (size_t v = 0; v < n; ++v) {
      const PackBoneBatch* batch = nullptr;
      for (const PackBoneBatch& b : sr.batches)
        if (v >= b.firstVertex && v < b.firstVertex + b.vertexCount) batch = &b;
      CHECK(batch != nullptr);
      float w[11] = {};
      for (uint32_t q = 0; q < batch->boneCount; ++q) w[batch->bones[q]] += sr.mesh.weights[v][q] / 128.0f;
      // Find a source vertex with the same position and normal and compare.
      for (size_t s = 0; s < n; ++s) {
        if ((g.mesh.positions[s] - sr.mesh.positions[v]).magnitude() > 1e-6f || (g.mesh.normals[s] - sr.mesh.normals[v]).magnitude() > 1e-6f) continue;
        float ref[11] = {};
        for (uint8_t e = 0; e < g.influences[s].count; ++e) ref[g.influences[s].joint[e]] += g.influences[s].weight[e];
        bool same = true;
        for (int j = 0; j < 11; ++j) same = same && fabsf(w[j] - ref[j]) < 1.0f / 64.0f;
        if (same) { ++matched; break; }
      }
    }
    CHECK_EQ(matched, n);
  }
}

TEST(cook3d_skin_split_drops_smallest_influences) {
  // One triangle whose three vertices use 12 distinct joints: it cannot fit 8, so the smallest influences go.
  LitMesh m;
  std::vector<SkinInfluences> inf(3);
  for (int k = 0; k < 3; ++k) {
    m.positions.push_back(Vector3(static_cast<float>(k), 0, 0));
    m.normals.push_back(Vector3(0, 0, 1));
    SkinInfluences& s = inf[static_cast<size_t>(k)];
    s.count = 4;
    for (int e = 0; e < 4; ++e) {
      s.joint[e] = static_cast<uint16_t>(k * 4 + e);
      s.weight[e] = e == 0 ? 0.7f : (e == 1 ? 0.2f : (e == 2 ? 0.07f : 0.03f));
    }
  }
  SkinSplitResult sr;
  std::string warn, err;
  CHECK(splitSkinnedMesh(m, inf, 8, sr, warn, err));
  CHECK(!warn.empty());
  CHECK_EQ(sr.batches.size(), size_t(1));
  CHECK(sr.batches[0].boneCount <= 8);
  CHECK(sr.droppedInfluences > 0);
  for (const auto& w : sr.mesh.weights) {
    int sum = 0;
    for (int q = 0; q < 8; ++q) sum += w[q];
    CHECK_EQ(sum, 128);
  }
  // The strongest influence of every vertex survives.
  for (size_t v = 0; v < 3; ++v) {
    bool has = false;
    for (uint32_t q = 0; q < sr.batches[0].boneCount; ++q) has = has || (sr.batches[0].bones[q] == v * 4 && sr.mesh.weights[v][q] > 60);
    CHECK(has);
  }
}

TEST(cook3d_quantize_weights_sum_128) {
  uint32_t seed = 12345;
  for (int iter = 0; iter < 500; ++iter) {
    float w[8], sum = 0;
    for (float& v : w) {
      seed = seed * 1664525u + 1013904223u;
      v = (seed >> 8) % 5 == 0 ? 0.0f : static_cast<float>((seed >> 8) & 255) / 255.0f;
      sum += v;
    }
    if (sum <= 0) continue;
    for (float& v : w) v /= sum;
    uint8_t q[8];
    quantizeWeights128(w, q);
    int total = 0;
    for (int i = 0; i < 8; ++i) {
      total += q[i];
      CHECK(fabsf(q[i] / 128.0f - w[i]) <= 1.0f / 128.0f + 1e-6f);
    }
    CHECK_EQ(total, 128);
  }
}

TEST(cook3d_c16_round_trip_error_bound) {
  ICookerFormatPlugin* psp = plugin("psp");
  CHECK(psp != nullptr);
  GltfModel g = importWalk();
  SkinSplitResult sr;
  std::string warn, err;
  CHECK(splitSkinnedMesh(g.mesh, g.influences, 8, sr, warn, err));
  for (int skinned = 0; skinned < 2; ++skinned) {
    LitMesh lm = skinned ? sr.mesh : g.mesh;
    if (!skinned) lm.weights.clear();
    std::vector<uint8_t> bytes;
    PackMeshLit d = {};
    CHECK(psp->cookLitMesh(lm, bytes, d, err));
    CHECK_EQ(d.vertexFormat, static_cast<uint32_t>(skinned ? PackVertexFormat::SkinC16 : PackVertexFormat::LitC16));
    const size_t stride = packVertexStride(d.vertexFormat);
    CHECK_EQ(bytes.size() % 16, size_t(0));
    CHECK(bytes.size() >= lm.positions.size() * stride);
    CHECK(d.decodeScale > 0);
    // The scale is a power of two.
    int e = 0;
    CHECK_NEAR(frexpf(d.decodeScale, &e), 0.5f, 1e-6f);
    float worstP = 0, worstN = 0, worstUv = 0;
    for (size_t i = 0; i < lm.positions.size(); ++i) {
      const uint8_t* v = bytes.data() + i * stride;
      PackVertexLitC16 c;
      if (skinned) {
        memcpy(&c, v + 8, sizeof c);
        for (int q = 0; q < 8; ++q) CHECK_EQ(v[q], lm.weights[i][q]);
      } else {
        memcpy(&c, v, sizeof c);
      }
      const float p[3] = {lm.positions[i].x, lm.positions[i].y, lm.positions[i].z};
      const float n[3] = {lm.normals[i].x, lm.normals[i].y, lm.normals[i].z};
      for (int k = 0; k < 3; ++k) {
        worstP = fmaxf(worstP, fabsf(d.decodeOffset[k] + c.p[k] * d.decodeScale - p[k]));
        worstN = fmaxf(worstN, fabsf(c.n[k] / 32767.0f - n[k]));
      }
      worstUv = fmaxf(worstUv, fabsf(c.uv[0] / 32768.0f - lm.uvs[i * 2]));
      worstUv = fmaxf(worstUv, fabsf(c.uv[1] / 32768.0f - lm.uvs[i * 2 + 1]));
    }
    CHECK(worstP <= d.decodeScale * 0.5f + 1e-6f);
    CHECK(worstN <= 1.0f / 32767.0f + 1e-6f);
    CHECK(worstUv <= 0.5f / 32768.0f + 1e-6f);
    // Bounds sphere holds every vertex.
    for (const Vector3& p : lm.positions)
      CHECK((p - Vector3(d.boundsCenter[0], d.boundsCenter[1], d.boundsCenter[2])).magnitude() <= d.boundsRadius + 1e-4f);
  }
  // UVs outside [0, 2) fall back to the float form; the platform plugins always write floats.
  LitMesh f = g.mesh;
  f.uvs[0] = 2.5f;
  std::vector<uint8_t> bytes;
  PackMeshLit d = {};
  CHECK(!uvFitsC16(f));
  CHECK(psp->cookLitMesh(f, bytes, d, err));
  CHECK_EQ(d.vertexFormat, static_cast<uint32_t>(PackVertexFormat::LitF));
  bytes.clear();
  CHECK(plugin("linux")->cookLitMesh(g.mesh, bytes, d, err));
  CHECK_EQ(d.vertexFormat, static_cast<uint32_t>(PackVertexFormat::LitF));
  CHECK_EQ(bytes.size() >= g.mesh.positions.size() * sizeof(PackVertexLit), true);
}

TEST(cook3d_material_mapping) {
  MaterialDef m;
  std::string err;
  CHECK(parseMaterialJson(R"({"format":1,"texture":"00000000-0000-4000-8000-000000000502","color":[10,20,30,40],"unlit":true,"alphaTest":true})", m, err));
  CHECK_EQ(m.textureGuid, std::string("00000000-0000-4000-8000-000000000502"));
  CHECK_EQ(colorAbgr(m.color), 0x281E140Au);
  CHECK_EQ(materialFlags(m), kMatUnlit | kMatAlphaTest);
  CHECK(parseMaterialJson("{}", m, err));  // every key has a default
  CHECK(m.textureGuid.empty());
  CHECK_EQ(colorAbgr(m.color), 0xFFFFFFFFu);
  CHECK_EQ(materialFlags(m), 0u);
  CHECK(!parseMaterialJson(R"({"color":[1,2,3]})", m, err));
  CHECK(!parseMaterialJson(R"({"color":[1,2,3,300]})", m, err));
  CHECK(!parseMaterialJson(R"({"texture":"nope"})", m, err));
  CHECK(!parseMaterialJson(R"({"format":2})", m, err));
  // Derived GUIDs: identity for 0, distinct otherwise, never equal to the source.
  Guid g;
  Guid::parse("00000000-0000-4000-8000-000000000501", g);
  CHECK(deriveGuid(g, 0) == g);
  CHECK(deriveGuid(g, 1) != g);
  CHECK(deriveGuid(g, kDerivedMaterialSalt) != g);
  CHECK(deriveGuid(deriveGuid(g, 7), 7) == g);
}

TEST(cook3d_texture_downscale) {
  // 4x4 -> 2x2: each output pixel is the mean of a 2x2 block.
  ImageRgba img;
  img.width = img.height = 4;
  img.rgba.resize(4 * 4 * 4);
  for (uint32_t y = 0; y < 4; ++y)
    for (uint32_t x = 0; x < 4; ++x) {
      uint8_t* p = &img.rgba[(y * 4 + x) * 4];
      p[0] = static_cast<uint8_t>(x * 40 + y * 10);
      p[1] = 100;
      p[2] = static_cast<uint8_t>(y * 60);
      p[3] = 255;
    }
  ImageRgba s = boxDownscale(img, 2, 2);
  CHECK_EQ(s.width, 2u);
  CHECK_EQ(s.rgba.size(), size_t(16));
  CHECK_EQ(s.rgba[0], static_cast<uint8_t>((0 + 40 + 10 + 50 + 1) / 4));  // mean of 0, 40, 10, 50 = 25
  CHECK_EQ(s.rgba[1], 100);
  CHECK_EQ(s.rgba[3], 255);
  // A 1024x512 image the PSP cannot hold is scaled to 512x256 instead of failing.
  ImageRgba big;
  big.width = 1024;
  big.height = 512;
  big.rgba.assign(size_t(1024) * 512 * 4, 200);
  CookedTexture out;
  std::string err;
  CHECK(pspCookTexture(big, "rgba8", out, err));
  CHECK_EQ(out.width, 512u);
  CHECK_EQ(out.height, 256u);
  CHECK_EQ(out.storedWidth, 512u);
  CHECK_EQ(out.storedHeight, 256u);
  // The PNG size reader.
  uint32_t w = 0, h = 0;
  CHECK(pngSize(readAll(fs::path(RENSGA_FIXTURES_DIR) / "gltf" / "blocky_walk.png"), w, h));
  CHECK_EQ(w, 64u);
  CHECK_EQ(h, 64u);
}

TEST(cook3d_decode_cooked_texture_round_trip) {
  // Cook with the PSP forms, decode through the plugin, compare with the source.
  ImageRgba img;
  img.width = 20;
  img.height = 13;  // not a power of two: stored size is padded
  img.rgba.resize(size_t(20) * 13 * 4);
  for (uint32_t y = 0; y < 13; ++y)
    for (uint32_t x = 0; x < 20; ++x) {
      uint8_t* p = &img.rgba[(size_t(y) * 20 + x) * 4];
      p[0] = static_cast<uint8_t>((x % 4) * 80);
      p[1] = static_cast<uint8_t>((y % 2) * 200);  // 4 x 2 x 2 = 16 colors: CLUT4 is exact
      p[2] = static_cast<uint8_t>(((x + y) % 2) * 255);
      p[3] = 255;
    }
  ICookerFormatPlugin* psp = plugin("psp");
  for (const char* fmt : {"rgba8", "clut4", "clut8", "rgba5551", "rgba4444"}) {
    CookedTexture ct;
    std::string err;
    CHECK(pspCookTexture(img, fmt, ct, err));
    CHECK((ct.flags & kTexSwizzled) != 0);
    // Build the record and the blob the way the pack does: pixels, then palette.
    std::vector<uint8_t> blob(ct.pixels);
    while (blob.size() % 16) blob.push_back(0);
    PackTexRec rec = {};
    rec.width = ct.width;
    rec.height = ct.height;
    rec.format = ct.format;
    rec.pixelOffset = 0;
    rec.pixelBytes = static_cast<uint32_t>(ct.pixels.size());
    rec.storedWidth = static_cast<uint16_t>(ct.storedWidth);
    rec.storedHeight = static_cast<uint16_t>(ct.storedHeight);
    rec.flags = static_cast<uint16_t>(ct.flags);
    if (!ct.palette.empty()) {
      rec.paletteCount = static_cast<uint16_t>(ct.palette.size());
      rec.paletteOffset = static_cast<uint32_t>(blob.size());
      const uint8_t* pb = reinterpret_cast<const uint8_t*>(ct.palette.data());
      blob.insert(blob.end(), pb, pb + ct.palette.size() * 4);
    }
    ImageRgba dec;
    CHECK(psp->decodeCookedTexture(rec, blob.data(), dec, err));
    CHECK_EQ(dec.width, 20u);
    CHECK_EQ(dec.height, 13u);
    int tol = 0;
    if (!strcmp(fmt, "rgba5551")) tol = 255 / 31;
    if (!strcmp(fmt, "rgba4444")) tol = 255 / 15;
    int worst = 0;
    for (size_t i = 0; i < img.rgba.size(); ++i) worst = std::max(worst, std::abs(int(img.rgba[i]) - int(dec.rgba[i])));
    if (worst > tol) fprintf(stderr, "    format %s: worst channel error %d (limit %d)\n", fmt, worst, tol);
    CHECK(worst <= tol);
  }
  // A PC cook (RGBA8, linear) decodes through the same call; the PS2 CLUT form too.
  for (const char* id : {"linux", "ps2"}) {
    ICookerFormatPlugin* pl = plugin(id);
    CookedTexture ct;
    std::string err;
    TextureImport ti;
    CHECK(pl->cookTexture(img, ti, ct, err));
    std::vector<uint8_t> blob(ct.pixels);
    while (blob.size() % 16) blob.push_back(0);
    PackTexRec rec = {};
    rec.width = ct.width;
    rec.height = ct.height;
    rec.format = ct.format;
    rec.pixelBytes = static_cast<uint32_t>(ct.pixels.size());
    rec.storedWidth = static_cast<uint16_t>(ct.storedWidth);
    rec.storedHeight = static_cast<uint16_t>(ct.storedHeight);
    if (!ct.palette.empty()) {
      rec.paletteCount = static_cast<uint16_t>(ct.palette.size());
      rec.paletteOffset = static_cast<uint32_t>(blob.size());
      const uint8_t* pb = reinterpret_cast<const uint8_t*>(ct.palette.data());
      blob.insert(blob.end(), pb, pb + ct.palette.size() * 4);
    }
    ImageRgba dec;
    CHECK(pl->decodeCookedTexture(rec, blob.data(), dec, err));
    CHECK(dec.rgba == img.rgba);
  }
}

TEST(cook3d_mesh_flags) {
  // A unit cube from the primitive generator: no near-clip flag; a 40 m plane: near-clip flag.
  PrimitiveParams pp;
  pp.shape = PrimitiveShape::Plane;
  pp.size = Vector3(40, 0, 40);
  pp.cellSize = 40;
  ConsoleTargetData t;
  LitMesh big = litMeshFromPrimitive(generatePrimitive(pp, t));
  std::string note;
  const uint32_t flags = analyzeMeshFlags(big, note);
  CHECK((flags & kMeshNeedsNearClip) != 0);
  pp.cellSize = 1;
  pp.size = Vector3(8, 0, 8);
  LitMesh small = litMeshFromPrimitive(generatePrimitive(pp, t));
  CHECK((analyzeMeshFlags(small, note) & kMeshNeedsNearClip) == 0);
  // Primitive UVs are box-projected into [0, 1].
  CHECK(uvFitsC16(small));
  float lo = 1, hi = 0;
  for (float u : small.uvs) { lo = fminf(lo, u); hi = fmaxf(hi, u); }
  CHECK_NEAR(lo, 0.0f, 1e-5f);
  CHECK_NEAR(hi, 1.0f, 1e-5f);
  // glTF character: consistent winding is reported (the S0 assets are counter-clockwise).
  GltfModel g = importWalk();
  const uint32_t cf = analyzeMeshFlags(g.mesh, note);
  CHECK((cf & kMeshWindingCCW) != 0);
  CHECK(note.empty());
}

TEST(cook3d_room_cooks_for_psp_and_linux) {
  for (const char* target : {"psp", "linux"}) {
    const fs::path out = tempDir(target);
    CookOut co = cookRoom(target, out);
    CHECK(co.result.ok);
    if (!co.result.ok) continue;
    Loaded L;
    CHECK(L.open(co.result.packPath));
    const PackView& v = L.view;
    u32 meshN = 0, litN = 0, batchN = 0;
    v.records<PackMeshRec>(pack_section::MESH, &meshN);
    const PackMeshLit* lit = v.records<PackMeshLit>(pack_section::MSHL, &litN);
    const PackBoneBatch* batches = v.records<PackBoneBatch>(pack_section::BTCH, &batchN);
    CHECK(lit != nullptr);
    CHECK_EQ(litN, meshN);
    PackAssets a = v.assets();
    CHECK_EQ(a.skinCount, 1u);
    CHECK_EQ(a.clip3Count, 2u);
    CHECK(a.materialCount >= 3);  // room, character, pillar (Lights12 scene shares the pack)
    CHECK(v.consoleTargetExt() != nullptr);
    // The character: skinned, with 2+ batches of at most 8 bones, a skin and its walk clip.
    bool sawSkin = false, sawRoom = false;
    const bool psp = !strcmp(target, "psp");
    for (u32 i = 0; i < litN; ++i) {
      if (lit[i].isBaked()) continue;
      CHECK(lit[i].material == kInvalidIndex || lit[i].material < a.materialCount);
      if (lit[i].isSkinned()) {
        sawSkin = true;
        CHECK_EQ(lit[i].vertexFormat, static_cast<uint32_t>(psp ? PackVertexFormat::SkinC16 : PackVertexFormat::SkinF));
        CHECK(lit[i].batchCount >= 2);
        u32 covered = 0;
        for (u32 b = 0; b < lit[i].batchCount; ++b) {
          CHECK(batches[lit[i].firstBatch + b].boneCount <= 8);
          covered += batches[lit[i].firstBatch + b].vertexCount;
        }
        CHECK_EQ(covered, lit[i].vertexCount);
        CHECK_EQ(a.skins[lit[i].skin].jointCount, 11u);
      } else if (lit[i].vertexCount == 2000u * 3 / 2 * 1 || lit[i].vertexCount > 2000) {
        sawRoom = true;
        CHECK_EQ(lit[i].vertexFormat, static_cast<uint32_t>(psp ? PackVertexFormat::LitC16 : PackVertexFormat::LitF));
      }
    }
    CHECK(sawSkin);
    CHECK(sawRoom);
    Guid walk;
    Guid::parse("00000000-0000-4000-8000-000000000501", walk);
    const PackClip3* clip = a.findClip3(walk);
    CHECK(clip != nullptr);
    if (clip) CHECK_EQ(clip->frameCount, 30u);
    // Report the section sizes (the evidence the task asks for).
    printf("    %s: pack %zu bytes", target, L.bytes.size());
    for (u32 i = 0; i < L.view.header->sectionCount; ++i) {
      const PackSection& s = L.view.sections[i];
      char fc[5] = {char(s.fourcc & 255), char((s.fourcc >> 8) & 255), char((s.fourcc >> 16) & 255), char((s.fourcc >> 24) & 255), 0};
      if (!strcmp(fc, "MSHL") || !strcmp(fc, "VTX3") || !strcmp(fc, "BTCH") || !strcmp(fc, "MATL") || !strcmp(fc, "SKIN") || !strcmp(fc, "CLP3") ||
          !strcmp(fc, "BLOB") || !strcmp(fc, "TEXR"))
        printf(", %s %u", fc, s.size);
    }
    printf("\n");
  }
}

TEST(cook3d_baked_projects_keep_old_sections) {
  // A project with no material and no skin (the cook_ok fixture, scripts removed): no F5 section appears and the pack stays baked.
  const fs::path proj = tempDir("baked") / "proj";
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok", proj, fs::copy_options::recursive);
  fs::remove_all(proj / "Library");
  const fs::path scenePath = proj / "Assets" / "Scenes" / "test.scene";
  const std::vector<uint8_t> sceneBytes = readAll(scenePath);
  nlohmann::json scene = nlohmann::json::parse(std::string(sceneBytes.begin(), sceneBytes.end()));
  for (auto& o : scene["objects"]) {
    nlohmann::json keep = nlohmann::json::array();
    for (auto& c : o["components"])
      if (c["type"] != "TestMover") keep.push_back(c);
    o["components"] = keep;
  }
  std::ofstream(scenePath) << scene.dump(1);
  CookOptions opt;
  opt.project = proj;
  opt.target = "psp";
  opt.force = true;
  CookResult r = cookProject(opt, [](const CookMessage& m) {
    if (m.severity == CookMessage::Error) fprintf(stderr, "    [cook error] %s\n", m.text.c_str());
  });
  CHECK(r.ok);
  if (!r.ok) return;
  Loaded L;
  CHECK(L.open(r.packPath));
  for (uint32_t fc : {pack_section::MSHL, pack_section::VTX3, pack_section::BTCH, pack_section::MATL, pack_section::SKIN, pack_section::CLP3})
    CHECK(L.view.find(fc) == nullptr);
  CHECK(L.view.find(pack_section::CTG2) != nullptr);
  CHECK(L.view.find(pack_section::MSHX) != nullptr);  // the compact baked form is unchanged
}
