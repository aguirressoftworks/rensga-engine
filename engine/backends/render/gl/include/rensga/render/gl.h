// OpenGL 3.3 (desktop) / GLES 3.0 (Android) renderer. Engine 0: clear + present only; meshes land in Engine 2/7.
// GL entry points are resolved through SDL_GL_GetProcAddress (no loader dependency).
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

class GlRenderer : public IRenderer {
 public:
  bool init(u32 width, u32 height) override;
  void setMeshes(const MeshTable&) override {}
  void beginFrame(Color32 clear) override;
  void setCamera(const Matrix4&, const Matrix4&) override {}
  void drawMesh(u32, const Matrix4&, u32) override {}
  void drawOverlayQuad(i32, i32, i32, i32, Color32) override {}
  void endFrame() override;
  u32 width() const override { return w_; }
  u32 height() const override { return h_; }

 private:
  u32 w_ = 0, h_ = 0;
  void* clearColor_ = nullptr;
  void* clear_ = nullptr;
  void* viewport_ = nullptr;
};

}  // namespace rensga
