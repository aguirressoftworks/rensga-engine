// F5 test fixture: writes a v2 pack with the shared-3D sections (MSHL, VTX3, BTCH, MATL, SKIN, CLP3, CTG2) by hand, so the
// renderers and the core tests do not wait for the cooker. Header only; link rensga_core (reflection is read for FDAT blobs).
//
// Contents (Pack3dBuilder::build()):
//   objects  0 Camera (Camera) at (0,1,6) | 1 Cube (MeshRenderer, material) | 2 Bar (SkinnedMeshRenderer + Animation) | 3 Sun (Light)
//   mesh 0   a unit cube, LitF, 36 vertices, material 0 (texture 0 = 4x4 RGBA8 checker)
//   mesh 1   a 2-joint bar (x,z in -0.25..0.25, y 0..2), SkinF, 72 vertices, 1 batch, skin 0; vertices on y == 1 weigh 64/64
//   skin 0   joint 0 root at the origin, joint 1 child at (0,1,0); clip 0: 2 frames at 30 fps, joint 1 bends 45 degrees about Z in frame 1
//   CTG2     maxLightsPerObject 4, maxBonesPerDraw 8, maxSkinnedJoints 64, maxTextureSize 512, all features
#pragma once
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "rensga/core/components.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/reflect.h"

namespace rensga {
namespace fixtures {

// 16-byte aligned owner of the finished pack bytes.
struct PackImage {
  u8* data = nullptr;
  size_t size = 0;
  PackImage() = default;
  PackImage(const PackImage&) = delete;
  PackImage& operator=(const PackImage&) = delete;
  ~PackImage() { free(data); }
  void assign(const std::vector<u8>& bytes) {
    free(data);
    size = bytes.size();
    data = static_cast<u8*>(aligned_alloc(16, (size + 15) & ~size_t(15)));
    memcpy(data, bytes.data(), size);
  }
};

class Pack3dBuilder {
 public:
  // What to leave out (to test the optional-section paths).
  bool withLit = true;       // MSHL / VTX3 / BTCH / MATL / SKIN / CLP3
  bool withCtg2 = true;
  bool c16Cube = false;      // write the cube as LitC16 instead of LitF

  static Guid guidOf(u32 n) {
    Guid g;
    char t[40];
    snprintf(t, sizeof t, "00000000-0000-4000-8000-%012x", n);
    Guid::parse(t, g);
    return g;
  }
  static Guid materialGuid() { return guidOf(0xa001); }
  static Guid clipGuid() { return guidOf(0xa002); }
  static Guid cubeMeshGuid() { return guidOf(0xa003); }
  static Guid barMeshGuid() { return guidOf(0xa004); }

  void build(PackImage& out) {
    strs_.assign(1, 0);  // string 0 is ""
    secs_.clear();
    blob_.clear();
    const u32 nameCamera = str("Camera"), nameCube = str("Cube"), nameBar = str("Bar"), nameSun = str("Sun");

    PackObject objs[4] = {};
    PackTransform xf[4] = {};
    const u32 names[4] = {nameCamera, nameCube, nameBar, nameSun};
    for (int i = 0; i < 4; ++i) {
      objs[i].guid = guidOf(0x100 + i);
      objs[i].nameStr = names[i];
      objs[i].parentIndex = -1;
      objs[i].active = 1;
      xf[i].rotation[3] = 1;
      xf[i].scale[0] = xf[i].scale[1] = xf[i].scale[2] = 1;
    }
    xf[0].position[1] = 1; xf[0].position[2] = 6;
    xf[1].position[0] = -1.5f;
    xf[2].position[0] = 1.5f;
    // Sun shines down-forward: rotate about X by -60 degrees.
    xf[3].rotation[0] = sinf(-0.5235988f); xf[3].rotation[3] = cosf(-0.5235988f);

    // Components.
    std::vector<PackComponent> comps;
    std::vector<u8> fdat;
    auto addComp = [&](const char* type, u32 obj, const void* instance) {
      const TypeInfo* t = TypeRegistry::instance().find(type);
      PackComponent c = {};
      c.typeHash = t->hash;
      c.objectIndex = obj;
      c.fieldOffset = static_cast<u32>(fdat.size());
      c.fieldSize = blobSize(*t);
      fdat.resize(fdat.size() + c.fieldSize);
      writeBlob(*t, instance, fdat.data() + c.fieldOffset);
      comps.push_back(c);
    };
    {
      Camera cam;
      addComp("Camera", 0, &cam);
      MeshRenderer mr;
      mr.mesh = cubeMeshGuid();
      mr.material = materialGuid();
      addComp("MeshRenderer", 1, &mr);
      SkinnedMeshRenderer sm;
      sm.mesh = barMeshGuid();
      sm.material = materialGuid();
      addComp("SkinnedMeshRenderer", 2, &sm);
      Animation an;
      an.clip = clipGuid();
      addComp("Animation", 2, &an);
      Light li;
      li.type = 0;
      addComp("Light", 3, &li);
    }
    while (fdat.size() % 16) fdat.push_back(0);

    // MESH / VERT: the two meshes are non-baked, so MESH.vertexCount is 0 and VERT is empty (an old renderer draws nothing).
    PackMeshRec mesh[2] = {{0, 0, 0, 1}, {0, 0, 0, 2}};
    PackVertex vert0 = {};  // keep VERT non-empty so its count is valid for tools that index it

    addSection(pack_section::STRS, strs_.data(), strs_.size(), static_cast<u32>(countStrings()));
    addSection(pack_section::OBJS, objs, sizeof objs, 4);
    addSection(pack_section::XFRM, xf, sizeof xf, 4);
    addSection(pack_section::COMP, comps.data(), comps.size() * sizeof(PackComponent), static_cast<u32>(comps.size()));
    addSection(pack_section::FDAT, fdat.data(), fdat.size(), static_cast<u32>(comps.size()));
    addSection(pack_section::MESH, mesh, sizeof mesh, 2);
    addSection(pack_section::VERT, &vert0, sizeof vert0, 1);
    PackConsoleTarget ct = {};
    ct.profile = makeFourCC('P', 'S', 'P', ' ');
    ct.width = 480; ct.height = 272;
    ct.ramBytes = 24u << 20; ct.vramBytes = 2u << 20;
    ct.trianglesPerFrame = 20000; ct.maxObjects = 256; ct.maxComponentsPerType = 256;
    ct.audioSampleRate = 22050; ct.audioVoices = 16;
    addSection(pack_section::CTGT, &ct, sizeof ct, 1);

    if (withCtg2) {
      PackConsoleTargetExt e = {};
      e.maxLightsPerObject = 4; e.maxBonesPerDraw = 8; e.maxSkinnedJoints = 64; e.maxTextureSize = 512;
      e.features = 0xFF;
      addSection(pack_section::CTG2, &e, sizeof e, 1);
    }

    // Texture: 4x4 RGBA8 checker (TEXR + BLOB).
    PackTexRec tex = {};
    tex.guid = guidOf(0xb001);
    tex.width = tex.height = 4;
    tex.format = 0;
    tex.pixelOffset = blobAlloc(64);
    tex.pixelBytes = 64;
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) {
        const u8 v = ((x + y) & 1) ? 230 : 90;
        u8* p = &blob_[tex.pixelOffset + (y * 4 + x) * 4];
        p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
      }

    std::vector<u8> vtx3;
    std::vector<PackMeshLit> lit(2);
    std::vector<PackBoneBatch> batches;
    PackMaterial mat = {};
    PackSkin skin = {};
    PackClip3 clip = {};
    if (withLit) {
      // Cube.
      lit[0] = {};
      lit[0].vertexFormat = static_cast<u32>(c16Cube ? PackVertexFormat::LitC16 : PackVertexFormat::LitF);
      lit[0].vertexByteOffset = static_cast<u32>(vtx3.size());
      lit[0].vertexCount = 36;
      lit[0].material = 0;
      lit[0].skin = kInvalidIndex;
      lit[0].boundsRadius = 0.8660254f;
      if (c16Cube) {
        lit[0].decodeScale = 1.0f / 16384.0f;  // power of two: i16 16384 = 1.0, so +-0.5 is +-8192
      }
      writeBox(vtx3, lit[0], -0.5f, 0.5f, 0, 1, -0.5f, 0.5f, false, 0, 0);
      pad16(vtx3);
      // Bar: two segments.
      lit[1] = {};
      lit[1].vertexFormat = static_cast<u32>(PackVertexFormat::SkinF);
      lit[1].vertexByteOffset = static_cast<u32>(vtx3.size());
      lit[1].vertexCount = 72;
      lit[1].material = 0;
      lit[1].skin = 0;
      lit[1].firstBatch = 0;
      lit[1].batchCount = 1;
      lit[1].boundsCenter[1] = 1;
      lit[1].boundsRadius = 1.0607f;
      writeBox(vtx3, lit[1], -0.25f, 0.25f, 0, 1, -0.25f, 0.25f, true, 0, 1);
      writeBox(vtx3, lit[1], -0.25f, 0.25f, 1, 2, -0.25f, 0.25f, true, 1, 2);
      pad16(vtx3);

      PackBoneBatch b = {};
      b.firstVertex = 0; b.vertexCount = 72;
      b.bones[0] = 0; b.bones[1] = 1;
      for (int i = 2; i < 8; ++i) b.bones[i] = kBatchBoneUnused;
      b.boneCount = 2;
      batches.push_back(b);

      mat.guid = materialGuid();
      mat.texture = 0;
      mat.colorABGR = 0xFFFFFFFFu;
      mat.flags = 0;
      mat.reserved = 0;

      // Skin: joint 0 root, joint 1 child 1 unit up.
      PackJoint joints[2] = {};
      for (int i = 0; i < 2; ++i) {
        joints[i].parent = i - 1;
        joints[i].nameStr = str(i ? "Top" : "Root");
        joints[i].r[3] = 1;
        joints[i].s[0] = joints[i].s[1] = joints[i].s[2] = 1;
        for (int k = 0; k < 16; ++k) joints[i].invBind[k] = (k % 5 == 0) ? 1.0f : 0.0f;
      }
      joints[1].t[1] = 1;
      joints[1].invBind[13] = -1;
      skin.guid = guidOf(0xc001);
      skin.jointCount = 2;
      skin.jointsOffset = blobAlloc(sizeof joints);
      memcpy(&blob_[skin.jointsOffset], joints, sizeof joints);

      // Clip: 2 frames x 2 joints.
      PackJointPose poses[4] = {};
      for (int f = 0; f < 2; ++f)
        for (int j = 0; j < 2; ++j) {
          PackJointPose& p = poses[f * 2 + j];
          p.r[3] = 1;
          p.s = 1;
          if (j == 1) p.t[1] = 1;
        }
      poses[3].r[2] = sinf(0.3926991f);  // 45 degrees about Z = 2 * 22.5
      poses[3].r[3] = cosf(0.3926991f);
      clip.guid = clipGuid();
      clip.nameStr = str("Bend");
      clip.skin = 0;
      clip.frameCount = 2;
      clip.fps = 30;
      clip.loop = 1;
      clip.dataOffset = blobAlloc(sizeof poses);
      memcpy(&blob_[clip.dataOffset], poses, sizeof poses);

      // STRS grew (joint and clip names): rewrite its section below.
    }
    // Sections that must come after the late strings.
    secs_[0].bytes.assign(strs_.begin(), strs_.end());
    secs_[0].count = static_cast<u32>(countStrings());

    if (withLit) {
      addSection(pack_section::MSHL, lit.data(), lit.size() * sizeof(PackMeshLit), static_cast<u32>(lit.size()));
      addSection(pack_section::VTX3, vtx3.data(), vtx3.size(), static_cast<u32>(vtx3.size()));
      addSection(pack_section::BTCH, batches.data(), batches.size() * sizeof(PackBoneBatch), static_cast<u32>(batches.size()));
    }
    addSection(pack_section::BLOB, blob_.data(), blob_.size(), static_cast<u32>(blob_.size()));
    addSection(pack_section::TEXR, &tex, sizeof tex, 1);
    if (withLit) {
      addSection(pack_section::MATL, &mat, sizeof mat, 1);
      addSection(pack_section::SKIN, &skin, sizeof skin, 1);
      addSection(pack_section::CLP3, &clip, sizeof clip, 1);
    }
    out.assign(serialize());
  }

 private:
  struct Sec {
    u32 fourcc;
    std::vector<u8> bytes;
    u32 count;
  };
  std::vector<u8> strs_;
  std::vector<Sec> secs_;
  std::vector<u8> blob_;

  u32 str(const char* s) {
    const u32 off = static_cast<u32>(strs_.size());
    strs_.insert(strs_.end(), s, s + strlen(s) + 1);
    return off;
  }
  size_t countStrings() const {
    size_t n = 0;
    for (u8 c : strs_) n += c == 0;
    return n;
  }
  u32 blobAlloc(size_t bytes) {
    while (blob_.size() % 16) blob_.push_back(0);
    const u32 off = static_cast<u32>(blob_.size());
    blob_.resize(blob_.size() + bytes, 0);
    return off;
  }
  void addSection(u32 fourcc, const void* data, size_t bytes, u32 count) {
    Sec s;
    s.fourcc = fourcc;
    s.bytes.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + bytes);
    s.count = count;
    secs_.push_back(s);
  }
  static void pad16(std::vector<u8>& v) {
    while (v.size() % 16) v.push_back(0);
  }

  std::vector<u8> serialize() const {
    const size_t table = sizeof(PackHeader) + secs_.size() * sizeof(PackSection);
    std::vector<PackSection> ps(secs_.size());
    size_t off = (table + 15) & ~size_t(15);
    for (size_t i = 0; i < secs_.size(); ++i) {
      ps[i].fourcc = secs_[i].fourcc;
      ps[i].offset = static_cast<u32>(off);
      ps[i].size = static_cast<u32>(secs_[i].bytes.size());
      ps[i].count = secs_[i].count;
      off = (off + secs_[i].bytes.size() + 15) & ~size_t(15);
    }
    std::vector<u8> out(off, 0);
    PackHeader h = {};
    memcpy(h.magic, "RPAK", 4);
    h.version = kPackVersion;
    h.sectionCount = static_cast<u32>(secs_.size());
    h.alignment = 16;
    h.totalSize = static_cast<u32>(off);
    memcpy(out.data(), &h, sizeof h);
    memcpy(out.data() + sizeof h, ps.data(), ps.size() * sizeof(PackSection));
    for (size_t i = 0; i < secs_.size(); ++i) memcpy(out.data() + ps[i].offset, secs_[i].bytes.data(), secs_[i].bytes.size());
    return out;
  }

  // A box as 12 triangles. `skin`: write PackVertexSkin with weights for joint segment [y0, y1] (joint a at y0, b at y1, vertices on
  // the shared y == 1 plane weigh half and half; the first segment's top ring and the second's bottom ring are that plane).
  void writeBox(std::vector<u8>& out, const PackMeshLit& m, float x0, float x1, float y0, float y1, float z0, float z1, bool skin, int a,
                int b) {
    struct Face { float n[3]; float c[4][3]; };
    const Face faces[6] = {
        {{0, 0, 1}, {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}},
        {{0, 0, -1}, {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}},
        {{1, 0, 0}, {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}},
        {{-1, 0, 0}, {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}},
        {{0, 1, 0}, {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}},
        {{0, -1, 0}, {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}},
    };
    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const int tri[6] = {0, 1, 2, 0, 2, 3};
    for (const Face& f : faces)
      for (int t = 0; t < 6; ++t) {
        const int k = tri[t];
        const float* p = f.c[k];
        PackVertexLit v = {};
        v.u = uv[k][0]; v.v = uv[k][1];
        v.color = 0xFFFFFFFFu;
        v.n[0] = f.n[0]; v.n[1] = f.n[1]; v.n[2] = f.n[2];
        v.p[0] = p[0]; v.p[1] = p[1]; v.p[2] = p[2];
        if (skin) {
          u8 w[8] = {};
          // Segment-local joints: `a` is the lower joint, `b` the upper. y == 1 is shared by both segments.
          const bool atLow = p[1] <= y0 + 1e-4f, atHigh = p[1] >= y1 - 1e-4f;
          const bool shared = (y1 == 1.0f && atHigh) || (y0 == 1.0f && atLow);
          if (shared) { w[0] = 64; w[1] = 64; }
          else if (y1 == 1.0f) { w[0] = 128; }   // lower segment, away from the shared plane: joint 0 only
          else { w[1] = 128; }                  // upper segment: joint 1 only
          (void)a; (void)b;
          PackVertexSkin sv = {};
          memcpy(sv.w, w, 8);
          memcpy(&sv.u, &v, sizeof v);
          const size_t at = out.size();
          out.resize(at + sizeof sv);
          memcpy(&out[at], &sv, sizeof sv);
        } else if (m.vertexFormat == static_cast<u32>(PackVertexFormat::LitC16)) {
          PackVertexLitC16 cv = {};
          cv.uv[0] = static_cast<u16>(v.u * 32768.0f);
          cv.uv[1] = static_cast<u16>(v.v * 32768.0f);
          cv.color = v.color;
          for (int i = 0; i < 3; ++i) {
            cv.n[i] = static_cast<i16>(f.n[i] * 32767.0f);
            cv.p[i] = static_cast<i16>(roundf((v.p[i] - m.decodeOffset[i]) / m.decodeScale));
          }
          const size_t at = out.size();
          out.resize(at + sizeof cv);
          memcpy(&out[at], &cv, sizeof cv);
        } else {
          const size_t at = out.size();
          out.resize(at + sizeof v);
          memcpy(&out[at], &v, sizeof v);
        }
      }
  }
};

}  // namespace fixtures
}  // namespace rensga
