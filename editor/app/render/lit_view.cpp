#define GL_GLEXT_PROTOTYPES 1
#include "render/lit_view.h"

#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "app.h"
#include "panel_registry.h"
#include "panels/preview_state.h"
#include "rensga/core/animation.h"
#include "rensga/core/application.h"
#include "rensga/core/components.h"
#include "rensga/core/cull.h"
#include "rensga/core/lighting.h"
#include "rensga/editor/editor_world.h"
#include "rensga/editor/inspector_model.h"
#include "rensga/render/gl.h"

namespace rensga {
namespace editor {

namespace {

struct LitGl {
  GlRenderer gl;
  bool inited = false, failed = false;
  u32 w = 0, h = 0;
  uint64_t boundId = 0;
  bool boundConsoleTex = false;
  std::vector<u32> tex;
};
LitGl* g_lit[3] = {nullptr, nullptr, nullptr};  // never deleted: a destructor after the GL context is gone would crash

LitGl& litGl(LitSlot s) {
  LitGl*& p = g_lit[static_cast<int>(s)];
  if (!p) p = new LitGl();
  return *p;
}

LitStats g_stats;

Matrix4 toMatrix(const M4& m) {
  Matrix4 r;
  for (int i = 0; i < 16; ++i) r.m[i] = m[static_cast<size_t>(i)];
  return r;
}

Color32 color32FromABGR(u32 v) { return Color32(static_cast<u8>(v & 255), static_cast<u8>((v >> 8) & 255), static_cast<u8>((v >> 16) & 255), static_cast<u8>(v >> 24)); }
u8 mulU8(u8 a, u8 b) { return static_cast<u8>((static_cast<u32>(a) * b + 127) / 255); }
Scalar maxScale(const Matrix4& w) {
  const Vector3 s = w.lossyScale();
  const Scalar m = s.x > s.y ? s.x : s.y;
  return m > s.z ? m : s.z;
}

// Uploads the World's textures (as the target stores them when `consoleTex`, so palette banding and 16-bit colour show).
void bind(LitGl& lg, EditorWorld& ew, bool consoleTex, int w, int h) {
  const ConsoleTargetData& ct = ew.consoleTarget();
  (void)ct;
  if (!lg.inited) {
    lg.inited = lg.gl.init(static_cast<u32>(w), static_cast<u32>(h));
    lg.failed = !lg.inited;
    lg.w = static_cast<u32>(w); lg.h = static_cast<u32>(h);
    if (!lg.inited) return;
    lg.gl.setOffscreen(true);
  }
  lg.gl.releaseUserTextures();
  lg.gl.setMeshes(ew.loaded().meshes);
  const PackAssets& as = ew.loaded().assets;
  lg.tex.assign(as.textureCount, 0);
  for (u32 i = 0; i < as.textureCount; ++i) {
    const PackTexRec& t = as.textures[i];
    TextureDesc d;
    d.width = t.width; d.height = t.height; d.format = static_cast<u8>(t.format);
    d.pixels = as.texturePixels(t); d.palette = as.texturePalette(t);
    d.storedWidth = t.storedWidth; d.storedHeight = t.storedHeight; d.flags = t.flags;
    if (consoleTex && ew.consolePreview()) {
      char g[40];
      t.guid.format(g);
      if (const ConsolePreview::Texture* ctx = ew.consolePreview()->find(g))
        if (!ctx->rgba.rgba.empty() && ctx->rgba.width && ctx->rgba.height) {
          d.width = ctx->rgba.width; d.height = ctx->rgba.height; d.format = 0;
          d.pixels = ctx->rgba.rgba.data(); d.palette = nullptr;
          d.storedWidth = 0; d.storedHeight = 0; d.flags = 0;
        }
    }
    lg.tex[i] = lg.gl.uploadTexture(d);
  }
  lg.boundId = ew.id();
  lg.boundConsoleTex = consoleTex;
}

// The 3D half of renderWorld (loop.cpp) for one explicit camera: lights, fog, baked and lit meshes, skinned meshes. No 2D, no
// particles: the Scene view shows the 3D scene the way the console draws it.
void drawWorld(LitGl& lg, World& world, const PackView& pv, const Matrix4& view, const Matrix4& proj, Color32 clear, u32 maxLights, LitStats& st) {
  GlRenderer& gl = lg.gl;
  gl.beginFrame(clear);
  gl.setCamera(view, proj);
  const Frustum frustum = frustumFromViewProj(proj * view);
  u32 litCount = 0;
  const PackMeshLit* lit = pv.records<PackMeshLit>(pack_section::MSHL, &litCount);
  const PackAssets& assets = pv.assets();
  LightSet lights;
  gatherLights(world, lights);
  if (lit || lights.fogOn) {
    gl.setFrameLights(lights.lights, lights.count, lights.ambient);
    gl.setFog(lights.fogOn, lights.fogColor, lights.fogStart, lights.fogEnd);
  }
  st.lights = lights.count;
  st.fog = lights.fogOn;
  unsigned objects = 0;
  auto drawLit = [&](u32 meshIndex, const Guid& matGuid, Color32 tint, const Matrix4& w, const SkinnedMeshRenderer* smr) {
    const PackMeshLit& m = lit[meshIndex];
    const Vector3 center = w.transformPoint(Vector3(m.boundsCenter[0], m.boundsCenter[1], m.boundsCenter[2]));
    const Scalar radius = m.boundsRadius * maxScale(w);
    if (m.boundsRadius > 0 && !sphereInFrustum(frustum, center, radius)) return;
    DrawParams dp;
    const PackMaterial* mat = assets.findMaterial(matGuid);
    if (!mat && m.material != kInvalidIndex && m.material < assets.materialCount) mat = &assets.materials[m.material];
    dp.matColor = tint;
    if (mat) {
      if (mat->texture != kInvalidIndex && mat->texture < lg.tex.size()) dp.texture = lg.tex[mat->texture];
      const Color32 mc = color32FromABGR(mat->colorABGR);
      dp.matColor = Color32(mulU8(mc.r, tint.r), mulU8(mc.g, tint.g), mulU8(mc.b, tint.b), mulU8(mc.a, tint.a));
      dp.matFlags = mat->flags;
    }
    if (m.flags & kMeshLightBaked) dp.matFlags |= kMatUnlit;  // baked vertex lighting: the light is in the colors
    if (!(dp.matFlags & kMatUnlit)) {
      u8 idx[kMaxDrawLights];
      dp.lightCount = static_cast<u8>(selectLights(lights, center, radius, maxLights, idx));
      for (u32 i = 0; i < dp.lightCount; ++i) dp.lights[i] = idx[i];
    }
    if (smr && m.isSkinned()) dp.palette = AnimationSystem::palette(world, *smr, &dp.paletteCount);
    gl.drawMeshEx(meshIndex, w, 0, dp);
  };
  world.forEach<MeshRenderer>([&](MeshRenderer& mr) {
    GameObject* go = mr.gameObject();
    if (!go || mr.meshIndex == kInvalidIndex || !go->activeInHierarchy() || !mr.enabled()) return;
    ++objects;
    const Matrix4 w = go->transform.localToWorld();
    if (lit && mr.meshIndex < litCount && !lit[mr.meshIndex].isBaked()) drawLit(mr.meshIndex, mr.material, mr.color, w, nullptr);
    else gl.drawMesh(mr.meshIndex, w, 0);
  });
  world.forEach<SkinnedMeshRenderer>([&](SkinnedMeshRenderer& sr) {
    GameObject* go = sr.gameObject();
    if (!go || sr.meshIndex == kInvalidIndex || !go->activeInHierarchy() || !sr.enabled()) return;
    ++objects;
    const Matrix4 w = go->transform.localToWorld();
    if (lit && sr.meshIndex < litCount && !lit[sr.meshIndex].isBaked()) drawLit(sr.meshIndex, sr.material, Color32(255, 255, 255, 255), w, &sr);
    else gl.drawMesh(sr.meshIndex, w, 0);
  });
  gl.endFrame();
  st.draws = gl.lastDrawCalls();
  st.objects = objects;
}

// The World that stands for the open scene: the running one during Play in editor, else the edit-mode World.
EditorWorld* sceneWorld(App& a) {
  if (a.session.active()) return a.session.world() && a.session.world()->valid() ? a.session.world() : nullptr;
  a.editWorld.setRequireModule(false);  // a scene with no scripts needs no script module for its lit view
  return a.editWorld.valid() ? a.editWorld.world() : nullptr;
}

// `v`, `p`: the renderer's own convention (view = inverse of the camera's world matrix, OpenGL projection).
unsigned renderLit(App& a, LitSlot slot, int w, int h, const Matrix4* v, const Matrix4* p, const float clear[4]) {
  if (!litViewEnabled() || !a.loaded || w < 8 || h < 8) return 0;
  EditorWorld* ew = sceneWorld(a);
  if (!ew) return 0;
  PreviewOptions& po = previewOptions();
  const PreviewRules pr = effectivePreviewRules(a.targetModule(), consoleTargetFor(a.project.root));
  LitGl& lg = litGl(slot);
  if (lg.failed) return 0;
  if (!lg.inited || lg.boundId != ew->id() || lg.boundConsoleTex != po.consoleTextures) bind(lg, *ew, po.consoleTextures, w, h);
  if (!lg.inited) return 0;
  if (lg.w != static_cast<u32>(w) || lg.h != static_cast<u32>(h)) {
    lg.gl.setVirtualResolution(static_cast<u32>(w), static_cast<u32>(h));
    lg.w = static_cast<u32>(w); lg.h = static_cast<u32>(h);
  }
  const u32 maxLights = pr.maxLightsPerObject > 0 && po.lightLimit ? static_cast<u32>(pr.maxLightsPerObject) : 8u;
  ew->setMaxLightsPerObject(maxLights);
  Matrix4 vm, pm;
  if (v && p) {
    vm = *v; pm = *p;
  } else {  // the first active Camera of the World, as renderWorld picks it
    Camera* cam = nullptr;
    ew->world().forEach<Camera>([&](Camera& c) {
      if (!cam && c.gameObject() && c.enabled() && c.gameObject()->activeInHierarchy()) cam = &c;
    });
    if (!cam) return 0;
    vm = cam->transform()->localToWorld().inverse();
    pm = Matrix4::perspective(cam->fov, static_cast<Scalar>(w) / static_cast<Scalar>(h), cam->nearClip, cam->farClip);
  }
  LitStats st = g_stats;
  st.maxLights = maxLights;
  st.worldId = ew->id();
  st.frames++;
  const Color32 bg(static_cast<u8>(clear[0] * 255.0f + 0.5f), static_cast<u8>(clear[1] * 255.0f + 0.5f), static_cast<u8>(clear[2] * 255.0f + 0.5f), 255);
  GLint fbo = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  drawWorld(lg, ew->world(), ew->view(), vm, pm, bg, maxLights, st);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  g_stats = st;
  return lg.gl.colorTexture();
}

}  // namespace

bool& litViewEnabled() {
  static bool on = true;
  return on;
}

const LitStats& litStats() { return g_stats; }

unsigned litSceneTexture(App& a, LitSlot slot, int w, int h, const M4& view, const M4& proj) {
  const ImVec4 bg = brandToken("surface-2");
  const float clear[4] = {bg.x, bg.y, bg.z, 1};
  // the editor's left-handed camera as the renderer's (-z forward) pair: the same product, the same image as the overlays
  const Matrix4 vm = toMatrix(viewToRightHanded(view)), pm = toMatrix(projToRightHanded(proj));
  unsigned tex = renderLit(a, slot, w, h, &vm, &pm, clear);
  if (!tex) return 0;
  PreviewOptions& po = previewOptions();
  if (po.quantize || po.dither) tex = previewPostProcess(tex, w, h, po.quantize, po.dither, true);
  return tex;
}

unsigned litGameTexture(App& a, int w, int h) {
  const ImVec4 bg = brandToken("surface-2");
  const float clear[4] = {bg.x, bg.y, bg.z, 1};
  return renderLit(a, LitSlot::Game, w, h, nullptr, nullptr, clear);
}

unsigned litCameraTexture(App& a, const std::string& objectGuid, int w, int h) {
  const SceneObject* o = a.doc.find(objectGuid);
  if (!o || !a.doc.loaded()) return 0;
  float fov = 60, nearClip = 0.3f, farClip = 200;
  for (const SceneComponent& c : o->components)
    if (c.type == "Camera") {
      fov = c.fields.value("fov", 60.0f);
      nearClip = c.fields.value("nearClip", 0.3f);
      farClip = c.fields.value("farClip", 200.0f);
    }
  double wd[16];
  a.doc.worldMatrix(objectGuid, wd);
  Matrix4 world;
  for (int i = 0; i < 16; ++i) world.m[i] = static_cast<Scalar>(wd[i]);
  const Matrix4 vm = world.inverse();
  const Matrix4 pm = Matrix4::perspective(fov, static_cast<Scalar>(w) / static_cast<Scalar>(std::max(h, 1)), nearClip, farClip);
  const ImVec4 bg = brandToken("surface-2");
  const float clear[4] = {bg.x, bg.y, bg.z, 1};
  return renderLit(a, LitSlot::Thumb, w, h, &vm, &pm, clear);
}


// --ui-event verbs: scenelit:on|off (the lit Scene view) | litdump (prints "lit: ...": what the last lit frame drew)
namespace {
bool litHook(App& app, const std::string& act, const std::string& arg) {
  if (act == "scenelit") {
    litViewEnabled() = arg != "off";
    std::printf("lit: scene view %s\n", litViewEnabled() ? "on" : "off");
    return true;
  }
  if (act == "scenecam") {  // scenecam:<ex>,<ey>,<ez>|<tx>,<ty>,<tz>[|<fov>]: the Scene view camera at the eye, looking at the target
    float e[3], t[3], fov = 0;
    if (std::sscanf(arg.c_str(), "%f,%f,%f|%f,%f,%f|%f", &e[0], &e[1], &e[2], &t[0], &t[1], &t[2], &fov) >= 6) {
      App& ap = app;
      const float d[3] = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
      const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (len > 1e-4f) {
        ap.sv.cam.pivot[0] = t[0]; ap.sv.cam.pivot[1] = t[1]; ap.sv.cam.pivot[2] = t[2];
        ap.sv.cam.distance = len;
        ap.sv.cam.yaw = std::atan2(d[0], d[2]);
        ap.sv.cam.pitch = std::asin(d[1] / len);
        if (fov > 1) ap.sv.cam.fovDeg = fov;
        ap.sv.needFrame = false;
        ap.sv.camDirty = true;
      }
    }
    return true;
  }
  if (act == "litdump") {
    const LitStats& st = litStats();
    std::printf("lit: frames=%u draws=%u objects=%u lights=%u maxLights=%u fog=%d world=%llu enabled=%d\n", st.frames, st.draws, st.objects, st.lights,
                st.maxLights, st.fog ? 1 : 0, st.worldId, litViewEnabled() ? 1 : 0);
    return true;
  }
  return false;
}
}  // namespace
RENSGA_EDITOR_PANEL(lit_view_hooks, "Lit view", nullptr, litHook, nullptr, 890, 137, ::rensga::editor::PanelDraw::None)

}  // namespace editor
}  // namespace rensga
