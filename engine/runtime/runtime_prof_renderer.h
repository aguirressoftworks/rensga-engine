// A forwarding IRenderer for the runtime mains (F4.06). renderWorld takes this instead of the real renderer, so that
//   1. the FrameProfiler gets its Render mark exactly when the frame's draw calls are all submitted, just before the
//      renderer's endFrame (which swaps and waits for vsync; that part is charged to Present), and
//   2. a main can draw its own overlay (the sprite stress, the touch zones) after every other draw without using
//      setOverlayHook: that hook is one global slot and Hud::Quad() silently takes it over.
// Uploads, setMeshes and init go straight to the real renderer, so the main keeps using the real one for those.
#pragma once
#include "profiler.h"
#include "rensga/core/backends.h"

namespace rensga {

class ProfilingRenderer : public IRenderer {
 public:
  using EndHook = void (*)(IRenderer& inner, void* user);
  ProfilingRenderer(IRenderer& inner, FrameProfiler& prof) : in_(inner), prof_(prof) {}
  void setEndHook(EndHook hook, void* user = nullptr) { hook_ = hook; user_ = user; }

  bool init(u32 w, u32 h) override { return in_.init(w, h); }
  void setMeshes(const MeshTable& t) override { table_ = t; in_.setMeshes(t); }
  // F5.11: the mains upload meshes to the real renderer; this only tells the wrapper the table so it can count triangles per draw.
  void observeMeshes(const MeshTable& t) { table_ = t; }
  void beginFrame(Color32 clear) override { in_.beginFrame(clear); }
  void setCamera(const Matrix4& v, const Matrix4& p) override { in_.setCamera(v, p); }
  void drawMesh(u32 mesh, const Matrix4& world, u32 key) override { count(mesh); in_.drawMesh(mesh, world, key); }
  void setFrameLights(const RenderLight* lights, u32 count, Color32 ambient) override { in_.setFrameLights(lights, count, ambient); }
  void setFog(bool on, Color32 color, Scalar start, Scalar end) override { in_.setFog(on, color, start, end); }
  void drawMeshEx(u32 mesh, const Matrix4& world, u32 key, const DrawParams& p) override { count(mesh); in_.drawMeshEx(mesh, world, key, p); }
  void drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 c) override { in_.drawOverlayQuad(x, y, w, h, c); }
  void endFrame() override {
    if (hook_) hook_(in_, user_);
    prof_.mark(ProfSection::Render);
    prof_.noteDraws(draws_, tris_);  // F5.11 scene budget counters
    draws_ = tris_ = 0;
    in_.endFrame();
  }
  u32 width() const override { return in_.width(); }
  u32 height() const override { return in_.height(); }
  u32 uploadTexture(const TextureDesc& d) override { return in_.uploadTexture(d); }
  void drawSprite(u32 tex, const Rect& src, const Rect& dst, Color32 tint, u32 key) override { in_.drawSprite(tex, src, dst, tint, key); }
  void drawRect(const Rect& dst, Color32 c, u32 key) override { in_.drawRect(dst, c, key); }
  void drawText(u32 font, const PackFontRec& f, i32 x, i32 y, const char* t, Color32 c, u32 key) override {
    in_.drawText(font, f, x, y, t, c, key);
  }
  void setVirtualResolution(u32 w, u32 h) override { in_.setVirtualResolution(w, h); }

 private:
  void count(u32 mesh) {
    ++draws_;
    if (mesh >= table_.meshCount) return;
    const u32 v = table_.lit && !table_.lit[mesh].isBaked() ? table_.lit[mesh].vertexCount : table_.meshes[mesh].vertexCount;
    tris_ += v / 3;
  }
  IRenderer& in_;
  FrameProfiler& prof_;
  MeshTable table_;
  u32 draws_ = 0, tris_ = 0;
  EndHook hook_ = nullptr;
  void* user_ = nullptr;
};

}  // namespace rensga
