// PS2 Graphics Synthesizer renderer through gsKit. Engine 0: clear + flip only; meshes land with Engine 7 / F8.
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

class GsRenderer : public IRenderer {
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
  void* gs_ = nullptr;
};

}  // namespace rensga
