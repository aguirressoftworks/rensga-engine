// OpenGL 3.3 core (desktop) / GLES 3.0 (Android) renderer, one shader source for both.
// GL entry points are resolved through SDL_GL_GetProcAddress (no loader dependency). Needs a current GL
// context (the SDL platform creates it).
//
// The frame is drawn into an offscreen FBO of the VIRTUAL resolution (init(w, h), or setVirtualResolution), then
// scaled into the window with integer-or-aspect letterboxing (computeLetterbox). 3D draws immediately with the
// depth buffer (vertex color x model-view-projection: the cooker bakes the lighting into vertex colors); 2D
// sprites, rects and text are queued in a fixed-capacity batch and drawn after the 3D, sorted by sortKey then
// texture. No heap allocation after init().
#pragma once
#include "rensga/core/backends.h"
#include "rensga/render/gl_batch.h"

namespace rensga {

class GlRenderer : public IRenderer {
 public:
  static constexpr u32 kSpriteCapacity = 16384;  // quads per frame; extra ones are dropped and counted
  static constexpr u32 kMaxTextures = 256;

  ~GlRenderer() override;
  void shutdown();  // frees GL objects; call before the platform destroys the context (static renderers)
  bool init(u32 width, u32 height) override;  // width x height = the virtual resolution
  void setMeshes(const MeshTable& table) override;
  void beginFrame(Color32 clear) override;
  void setCamera(const Matrix4& view, const Matrix4& proj) override;
  void drawMesh(u32 meshIndex, const Matrix4& world, u32 sortKey) override;
  // F5: lit / textured / skinned meshes (MSHL + VTX3). The lighting is per vertex (Gouraud, like the PSP GU) so the desktop
  // build looks like the PSP one: ambient + up to 8 directional / point lights (point: intensity * clamp01(1 - d / range)^2),
  // times vertex color x material color, times the texture (nearest), then linear fog by view-space depth. Skinning is on the
  // GPU: 8 weights per vertex, weight slot i uses DrawParams.palette[batch.bones[i]]. A baked mesh takes the drawMesh path.
  void setFrameLights(const RenderLight* lights, u32 count, Color32 ambient) override;
  void setFog(bool on, Color32 color, Scalar start, Scalar end) override;
  void drawMeshEx(u32 meshIndex, const Matrix4& world, u32 sortKey, const DrawParams& params) override;
  void drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) override;
  void endFrame() override;
  u32 width() const override { return w_; }
  u32 height() const override { return h_; }

  u32 uploadTexture(const TextureDesc& desc) override;
  void drawSprite(u32 tex, const Rect& src, const Rect& dst, Color32 tint, u32 sortKey) override;
  void drawRect(const Rect& dst, Color32 color, u32 sortKey) override;
  void drawText(u32 fontTex, const PackFontRec& font, i32 x, i32 y, const char* text, Color32 color,
                u32 sortKey) override;
  void setVirtualResolution(u32 w, u32 h) override;
  // Hot reload (F2.03): deletes every texture uploaded after the renderer's own 1x1 white one, so a reloaded pack
  // uploads its textures again under the same handles. Not for use inside a frame.
  void releaseUserTextures();

  // Embedded use (the editor's Game view): endFrame() draws the frame into the virtual-resolution target and
  // returns without presenting or swapping; colorTexture() is that image (GL texture name, bottom row first).
  void setOffscreen(bool on) { offscreen_ = on; }
  u32 colorTexture() const { return colorTex_; }
  // Writes the last presented virtual-resolution image (RGBA, upright) as a PNG. Call after endFrame().
  bool screenshot(const char* pngPath);
  // Stats of the last frame, for logs.
  u32 lastSpriteCount() const { return lastSprites_; }
  u32 lastDrawCalls() const { return lastDraws_; }
  u32 droppedSprites() const { return batch_.dropped(); }

 private:
  struct Tex {
    u32 id = 0, w = 0, h = 0;
    bool repeat = false;  // wrap mode currently set: sprites clamp (atlas edges), 3D meshes repeat (tiled UVs)
  };
  bool createTarget();
  void destroyTarget();
  void flush2d();
  bool compile(u32& program, const char* vs, const char* fs, const char* defines = "");
  void freeLitMeshes();
  void setWrap(Tex& t, bool repeat);
  void drawLitBatch(u32 vao, u32 first, u32 count);

  u32 w_ = 0, h_ = 0;
  bool ready_ = false;
  // GL object names
  u32 fbo_ = 0, colorTex_ = 0, depthRb_ = 0;
  u32 progMesh_ = 0, progSprite_ = 0, progPresent_ = 0;
  u32 vaoMesh_ = 0, vboMesh_ = 0, vaoSprite_ = 0, vboSprite_ = 0, vaoEmpty_ = 0;
  i32 uMvp_ = -1, uInvVirt_ = -1;
  u32 whiteTex_ = 0;  // handle of the 1x1 white texture used by rects
  Tex tex_[kMaxTextures];
  u32 texCount_ = 0;

  // F5 lit path. One VBO holds VTX3; every non-baked mesh has its own VAO (attribute offsets differ per mesh), created in
  // setMeshes() and freed on the next one. Uniform locations are resolved once in init().
  struct LitUniforms {
    i32 vp, world, view, decOff, decScale, uvScale, mat, ambient, numLights, lightPos, lightCol, lit, tex, alphaTest, blend,
        fogOn, fogColor, fogRange, bones;
  };
  void lookupLitUniforms(u32 program, LitUniforms& u);
  u32 progLit_ = 0, progSkin_ = 0;
  LitUniforms uLit_ = {}, uSkin_ = {};
  u32 vboLit_ = 0;
  u32* litVao_ = nullptr;  // meshes_.meshCount entries, 0 = none
  u32 litVaoCount_ = 0;
  RenderLight lights_[kMaxFrameLights];
  u32 lightCount_ = 0;
  float ambient_[3] = {0, 0, 0};
  bool fogOn_ = false;
  float fogColor_[3] = {0, 0, 0};
  float fogStart_ = 0, fogEnd_ = 1;
  float boneBuf_[kMaxBatchBones * 16];

  MeshTable meshes_;
  Matrix4 view_, proj_;
  bool cameraSet_ = false;
  bool depthOn_ = false;
  SpriteBatch batch_;
  u32 lastSprites_ = 0, lastDraws_ = 0;
  u32 winW_ = 0, winH_ = 0;
  bool warnedFull_ = false;
  bool offscreen_ = false;
};

}  // namespace rensga
