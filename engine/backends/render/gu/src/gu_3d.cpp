// F5.01 (PSP Engine 6, agent "gu3d"): the GuRenderer lit / textured / skinned 3D path. PSP target only.
//   - up to 4 hardware lights per draw (sceGuLight / LightColor / LightAtt), ambient from the frame, material color folded into the
//     light and ambient colors (the vertex color is the material, so vertex x matColor x light), kMatUnlit = ambient-only lighting
//   - 3D textures: nearest, repeat, modulate; UVs rescaled from source size to the padded stored size
//   - alpha test (alpha < 128 discarded), linear fog, back-face culling driven by kMeshWindingCCW
//   - CPU near-plane clipping in model space for kMeshNeedsNearClip meshes whose bounds cross the near plane
//   - hardware skinning: GU_WEIGHTS(8) | GU_WEIGHT_8BIT, one sceGuBoneMatrix(i, palette[batch.bones[i]]) set per BTCH batch
//   - C16 meshes: the decode offset and scale are folded into the model matrix (and the bone matrices)
// VFPU: -DRENSGA_PSP_VFPU_RENDER=1 concatenates render-side matrices with gumMultMatrix (VFPU). Never used by sim code.
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <string.h>

#include "gu_internal.h"
#include "rensga/core/log.h"
#include "rensga/render/gu.h"

namespace rensga {

namespace {
constexpr u32 kHwLights = 4;
constexpr u32 kClipVerts = 3072;  // clipped triangle vertices per frame (two buffers, one per frame parity)
alignas(64) PackVertexLit g_clip[2][kClipVerts];

inline void mul(Matrix4& r, const Matrix4& a, const Matrix4& b) {
#if defined(RENSGA_PSP_VFPU_RENDER) && RENSGA_PSP_VFPU_RENDER
  alignas(16) Matrix4 ta = a, tb = b, tr;
  gumMultMatrix(reinterpret_cast<ScePspFMatrix4*>(tr.m), reinterpret_cast<const ScePspFMatrix4*>(ta.m),
                reinterpret_cast<const ScePspFMatrix4*>(tb.m));
  r = tr;
#else
  r = a * b;
#endif
}

inline u32 packColor(float r, float g, float b, float a) {
  auto q = [](float v) { return static_cast<u32>(v <= 0 ? 0 : v >= 255 ? 255 : v + 0.5f); };
  return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}
inline u32 mix(u32 h, u32 v) { return (h ^ v) * 16777619u; }

// C16 -> model space: position = offset + i16 * scale; the GE reads the i16 as q / 32768, so the matrix scale is scale * 32768.
Matrix4 decodeMatrix(const PackMeshLit& ml) {
  const float s = ml.decodeScale * 32768.0f;
  Matrix4 m = Matrix4::identity();
  m.m[0] = s; m.m[5] = s; m.m[10] = s;
  m.m[12] = ml.decodeOffset[0]; m.m[13] = ml.decodeOffset[1]; m.m[14] = ml.decodeOffset[2];
  return m;
}

// One clipped-triangle vertex (float, any lit format decoded).
struct CV { float u, v; float c[4]; float n[3]; float p[3]; };

inline void loadCV(const PackMeshLit& ml, const u8* src, u32 i, CV& o) {
  if (ml.vertexFormat == static_cast<u32>(PackVertexFormat::LitC16)) {
    const PackVertexLitC16& v = reinterpret_cast<const PackVertexLitC16*>(src)[i];
    o.u = v.uv[0] * (1.0f / 32768.0f); o.v = v.uv[1] * (1.0f / 32768.0f);
    for (int k = 0; k < 4; ++k) o.c[k] = static_cast<float>((v.color >> (8 * k)) & 255);
    for (int k = 0; k < 3; ++k) {
      o.n[k] = v.n[k] * (1.0f / 32767.0f);
      o.p[k] = ml.decodeOffset[k] + v.p[k] * ml.decodeScale;
    }
  } else {
    const PackVertexLit& v = reinterpret_cast<const PackVertexLit*>(src)[i];
    o.u = v.u; o.v = v.v;
    for (int k = 0; k < 4; ++k) o.c[k] = static_cast<float>((v.color >> (8 * k)) & 255);
    for (int k = 0; k < 3; ++k) { o.n[k] = v.n[k]; o.p[k] = v.p[k]; }
  }
}
inline void lerpCV(const CV& a, const CV& b, float t, CV& o) {
  o.u = a.u + (b.u - a.u) * t; o.v = a.v + (b.v - a.v) * t;
  for (int k = 0; k < 4; ++k) o.c[k] = a.c[k] + (b.c[k] - a.c[k]) * t;
  for (int k = 0; k < 3; ++k) { o.n[k] = a.n[k] + (b.n[k] - a.n[k]) * t; o.p[k] = a.p[k] + (b.p[k] - a.p[k]) * t; }
}
inline void storeCV(const CV& c, PackVertexLit& o) {
  o.u = c.u; o.v = c.v;
  o.color = packColor(c.c[0], c.c[1], c.c[2], c.c[3]);
  for (int k = 0; k < 3; ++k) { o.n[k] = c.n[k]; o.p[k] = c.p[k]; }
}
}  // namespace

void GuRenderer::setFrameLights(const RenderLight* lights, u32 count, Color32 ambient) {
  lightCount_ = count > kMaxFrameLights ? kMaxFrameLights : count;
  for (u32 i = 0; i < lightCount_; ++i) lights_[i] = lights[i];
  ambient_ = ambient;
}

void GuRenderer::setFog(bool on, Color32 color, Scalar start, Scalar end) {
  fogOn_ = on;
  fogColor_ = color;
  fogStart_ = start;
  fogEnd_ = end;
}

void GuRenderer::enterBakedState() {
  if (mode3D_ == 1) return;
  sceGuDisable(GU_LIGHTING);
  sceGuDisable(GU_CULL_FACE);  // the baked winding is not guaranteed
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_ALPHA_TEST);
  mode3D_ = 1;
  curTex_ = 0;
  cullState_ = 0;
  alphaTest3D_ = false;
  hwMask_ = 0;
}

void GuRenderer::enterLitState() {
  if (mode3D_ == 2) return;
  sceGuEnable(GU_LIGHTING);
  sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);  // the vertex color is the ambient and diffuse material color
  sceGuModelColor(0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u);
  sceGuSpecular(1.0f);
  mode3D_ = 2;
}

void GuRenderer::leave3DState() {
  sceGuDisable(GU_LIGHTING);
  sceGuDisable(GU_CULL_FACE);
  sceGuDisable(GU_FOG);
  sceGuDisable(GU_ALPHA_TEST);
  sceGuDisable(GU_TEXTURE_2D);
  for (u32 i = 0; i < kHwLights; ++i) sceGuDisable(GU_LIGHT0 + i);
  mode3D_ = 0;
  hwMask_ = 0;
}

void GuRenderer::bind3DTexture(u32 handle) {
  if (handle == curTex_) return;
  curTex_ = handle;
  if (!handle || handle > texCount_) {
    sceGuDisable(GU_TEXTURE_2D);
    curTex_ = 0;
    return;
  }
  const Tex& t = tex_[handle - 1];
  bindTexture(t);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_REPEAT, GU_REPEAT);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  // UVs are in source-size space (0..1); the stored array may be padded to a bigger power of two.
  const float su = t.srcW && t.w ? static_cast<float>(t.srcW) / t.w : 1.0f, sv = t.srcH && t.h ? static_cast<float>(t.srcH) / t.h : 1.0f;
  sceGuTexScale(su, sv);
  sceGuTexOffset(0.0f, 0.0f);
}

// Sends the lights of one draw. Lit: slot j = frame light params.lights[j], color = light color x intensity x material color.
// Unlit: no lights, ambient = material color (vertex color x material color, nothing else).
void GuRenderer::applyLights(const DrawParams& p, bool unlit) {
  const u32 n = unlit ? 0 : (p.lightCount < kHwLights ? p.lightCount : kHwLights);
  u32 sig = mix(2166136261u, n + (unlit ? 100u : 0u));
  sig = mix(sig, p.matColor.packedABGR());
  for (u32 j = 0; j < n; ++j) sig = mix(sig, p.lights[j]);
  if (sig == lightSig_) return;
  lightSig_ = sig;
  const float mr = p.matColor.r * (1.0f / 255.0f), mg = p.matColor.g * (1.0f / 255.0f), mb = p.matColor.b * (1.0f / 255.0f);
  const float amr = unlit ? 255.0f : static_cast<float>(ambient_.r), amg = unlit ? 255.0f : static_cast<float>(ambient_.g),
              amb = unlit ? 255.0f : static_cast<float>(ambient_.b);
  sceGuAmbient(packColor(amr * mr, amg * mg, amb * mb, static_cast<float>(p.matColor.a)));
  u32 mask = 0;
  for (u32 j = 0; j < n; ++j) {
    const u32 idx = p.lights[j];
    if (idx >= lightCount_) continue;
    const RenderLight& l = lights_[idx];
    ScePspFVector3 v;
    if (l.type == 0) {  // directional: the GE wants the direction TO the light
      v.x = -l.vec.x; v.y = -l.vec.y; v.z = -l.vec.z;
      sceGuLight(static_cast<int>(j), GU_DIRECTIONAL, GU_DIFFUSE, &v);
      sceGuLightAtt(static_cast<int>(j), 1.0f, 0.0f, 0.0f);
    } else {
      v.x = l.vec.x; v.y = l.vec.y; v.z = l.vec.z;
      sceGuLight(static_cast<int>(j), GU_POINTLIGHT, GU_DIFFUSE, &v);
      // 1 / (1 + 9 d^2 / range^2): full at the light, one tenth at `range`.
      const float q = l.range > 0.001f ? 9.0f / (l.range * l.range) : 0.0f;
      sceGuLightAtt(static_cast<int>(j), 1.0f, 0.0f, q);
    }
    const float k = static_cast<float>(l.intensity);
    sceGuLightColor(static_cast<int>(j), GU_DIFFUSE, packColor(l.color.r * k * mr, l.color.g * k * mg, l.color.b * k * mb, 255.0f));
    mask |= 1u << j;
    ++litSlots_;
  }
  for (u32 j = 0; j < kHwLights; ++j) {
    const u32 bit = 1u << j;
    if ((mask & bit) && !(hwMask_ & bit)) sceGuEnable(GU_LIGHT0 + j);
    else if (!(mask & bit) && (hwMask_ & bit)) sceGuDisable(GU_LIGHT0 + j);
  }
  hwMask_ = mask;
}

void GuRenderer::drawLit(const Draw& d, const PackMeshLit& ml) {
  const u32 stride = packVertexStride(ml.vertexFormat);
  if (!stride || !ml.vertexCount || ml.vertexByteOffset + ml.vertexCount * stride > table_.vtx3Size) return;
  const DrawParams& p = d.params;
  const bool unlit = (p.matFlags & kMatUnlit) != 0;
  const bool c16 = ml.isC16(), skinned = ml.isSkinned();
  const u8* src = table_.vtx3 + ml.vertexByteOffset;

  // Near-plane / guard-band handling (static lit meshes flagged kMeshNeedsNearClip): the GE drops a triangle with a vertex behind the
  // eye or projected far outside its 4096x4096 drawing area, so a big mesh near the camera is clipped on the CPU, in model space,
  // against the near plane and the four sides (widened to 2.5x the screen), and the result is drawn as float vertices.
  // A mesh wholly behind the camera is skipped; one whose nearest point is far enough away is drawn as it is.
  const PackVertexLit* clipped = nullptr;
  u32 clippedCount = 0;
  if (!skinned && (ml.flags & kMeshNeedsNearClip)) {
    Matrix4 vw;
    mul(vw, view_, d.world);
    const float nearD = proj_.m[14] / (proj_.m[10] - 1.0f);  // OpenGL-style perspective (math.h)
    if (nearD > 0.0f) {
      const float zc = vw.m[2] * ml.boundsCenter[0] + vw.m[6] * ml.boundsCenter[1] + vw.m[10] * ml.boundsCenter[2] + vw.m[14];
      float sc = 0;  // largest column scale of the world matrix
      for (int c = 0; c < 3; ++c) {
        const float l2 = vw.m[c * 4] * vw.m[c * 4] + vw.m[c * 4 + 1] * vw.m[c * 4 + 1] + vw.m[c * 4 + 2] * vw.m[c * 4 + 2];
        if (l2 > sc) sc = l2;
      }
      const float r = ml.boundsRadius * Mathf::Sqrt(sc);
      const float zmin = -zc - r;  // view depth of the nearest point of the bounding sphere
      if (-zc + r < nearD) return;  // wholly behind the camera
      if (zmin < nearD || zmin < 0.25f * r) {
        Matrix4 pv, mvp;
        mul(pv, proj_, view_);
        mul(mvp, pv, d.world);
        constexpr float kSide = 2.5f;
        float pl[5][4];  // inside: dot(pl, (p, 1)) >= 0
        for (int c = 0; c < 4; ++c) {
          const float r0 = mvp.m[c * 4 + 0], r1 = mvp.m[c * 4 + 1], r2 = mvp.m[c * 4 + 2], r3 = mvp.m[c * 4 + 3];
          pl[0][c] = r0 + kSide * r3;
          pl[1][c] = kSide * r3 - r0;
          pl[2][c] = r1 + kSide * r3;
          pl[3][c] = kSide * r3 - r1;
          pl[4][c] = r2 + r3;  // z_c >= -w: the near plane
        }
        pl[4][3] -= 0.01f;
        PackVertexLit* out = g_clip[frame_ & 1];
        u32 outN = 0;
        bool full = false;
        for (u32 t = 0; t + 2 < ml.vertexCount && !full; t += 3) {
          CV poly[2][10];
          u32 pn = 3;
          for (int k = 0; k < 3; ++k) loadCV(ml, src, t + k, poly[0][k]);
          u32 cur = 0;
          for (int e = 0; e < 5 && pn >= 3; ++e) {
            const float* q = pl[e];
            float dd[10];
            bool any = false, all = true;
            for (u32 k = 0; k < pn; ++k) {
              const CV& v = poly[cur][k];
              dd[k] = q[0] * v.p[0] + q[1] * v.p[1] + q[2] * v.p[2] + q[3];
              if (dd[k] >= 0) any = true; else all = false;
            }
            if (!any) { pn = 0; break; }
            if (all) continue;
            CV* dst = poly[cur ^ 1];
            u32 n2 = 0;
            for (u32 k = 0; k < pn; ++k) {
              const u32 j = (k + 1) % pn;
              if (dd[k] >= 0) dst[n2++] = poly[cur][k];
              if ((dd[k] >= 0) != (dd[j] >= 0)) lerpCV(poly[cur][k], poly[cur][j], dd[k] / (dd[k] - dd[j]), dst[n2++]);
            }
            pn = n2;
            cur ^= 1;
          }
          if (pn < 3) continue;
          if (outN + (pn - 2) * 3 > kClipVerts) { full = true; break; }
          for (u32 k = 1; k + 1 < pn; ++k) {
            storeCV(poly[cur][0], out[outN++]);
            storeCV(poly[cur][k], out[outN++]);
            storeCV(poly[cur][k + 1], out[outN++]);
          }
        }
        if (full) {
          ++nearClipFallbacks_;  // buffer full: draw the mesh unclipped (hardware clipping)
        } else {
          if (outN == 0) return;
          sceKernelDcacheWritebackRange(out, outN * sizeof(PackVertexLit));
          clipped = out;
          clippedCount = outN;
          ++clipped_;
        }
      }
    }
  }

  enterLitState();
  ++litDraws_;

  // Culling: kMeshWindingCCW = triangles wound counter-clockwise seen from outside (glTF / GL order, what the cooker writes).
  const u32 wantCull = (ml.flags & kMeshWindingCCW) ? 2 : 1;
#ifdef RENSGA_GU_NOCULL  // dev: see every face (checks the winding of a cooked mesh)
  if (cullState_ != 9) { sceGuDisable(GU_CULL_FACE); cullState_ = 9; }
  if (false) {
#else
  if (cullState_ != wantCull) {
#endif
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(wantCull == 2 ? GU_CCW : GU_CW);  // CCW = counter-clockwise seen from outside in the right-handed model space (glTF order)
    cullState_ = wantCull;
  }
  bind3DTexture(p.texture);
  const bool at = (p.matFlags & kMatAlphaTest) != 0;
  if (at != alphaTest3D_) {
    if (at) {
      sceGuEnable(GU_ALPHA_TEST);
      sceGuAlphaFunc(GU_GEQUAL, 128, 0xFF);
    } else {
      sceGuDisable(GU_ALPHA_TEST);
    }
    alphaTest3D_ = at;
  }
  applyLights(p, unlit);

  Matrix4 model = d.world, dec = Matrix4::identity();
  if (c16) {
    dec = decodeMatrix(ml);
    if (!skinned && !clipped) mul(model, d.world, dec);  // a clipped copy is decoded float already
  }
  sceGumMatrixMode(GU_MODEL);
  sceGumLoadMatrix(reinterpret_cast<const ScePspFMatrix4*>(model.m));
  sceGumUpdateMatrix();

  if (clipped) {
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    clippedCount, nullptr, clipped);
    triangles_ += clippedCount / 3;
    return;
  }

  if (!skinned) {
    const u32 type = c16 ? (GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_NORMAL_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_3D)
                         : (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D);
    sceGumDrawArray(GU_TRIANGLES, type, ml.vertexCount, nullptr, src);
    triangles_ += ml.vertexCount / 3;
    return;
  }

  // Skinned: one draw per batch; GU weight i uses bone matrix i, loaded from palette[batch.bones[i]] (x decode for C16).
  const u32 type = (c16 ? (GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_NORMAL_16BIT | GU_VERTEX_16BIT)
                        : (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF)) |
                   GU_WEIGHT_8BIT | GU_WEIGHTS(8) | GU_TRANSFORM_3D;
  const bool haveBatches = table_.batches && ml.batchCount && ml.firstBatch + ml.batchCount <= table_.batchCount;
  const u32 nb = haveBatches ? ml.batchCount : 1;
  for (u32 b = 0; b < nb; ++b) {
    PackBoneBatch dflt = {};
    const PackBoneBatch* bt;
    if (haveBatches) {
      bt = &table_.batches[ml.firstBatch + b];
    } else {
      dflt.firstVertex = 0; dflt.vertexCount = ml.vertexCount;
      for (u32 i = 0; i < 8; ++i) dflt.bones[i] = static_cast<u8>(i);
      dflt.boneCount = 8;
      bt = &dflt;
    }
    if (bt->firstVertex + bt->vertexCount > ml.vertexCount || !bt->vertexCount) continue;
    for (u32 i = 0; i < 8; ++i) {
      const u8 j = bt->bones[i];
      if (j == kBatchBoneUnused) continue;
      Matrix4 bone = Matrix4::identity();
      if (p.palette && j < p.paletteCount) bone = p.palette[j];
      Matrix4 bm;
      if (c16) mul(bm, bone, dec);
      else bm = bone;
      sceGuBoneMatrix(i, reinterpret_cast<const ScePspFMatrix4*>(bm.m));
    }
    sceGumDrawArray(GU_TRIANGLES, type, bt->vertexCount, nullptr, src + bt->firstVertex * stride);
    triangles_ += bt->vertexCount / 3;
    ++batchesDrawn_;
  }
}

}  // namespace rensga
