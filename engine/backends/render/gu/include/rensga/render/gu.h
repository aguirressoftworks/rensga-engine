// GU renderer (MVP-25, -26): two 32-bit framebuffers + 16-bit depth in VRAM, one display list per
// frame, perspective via sceGum, meshes drawn in sortKey order, overlay quads through the same interface.
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

class GuRenderer : public IRenderer {
 public:
  static constexpr u32 kMaxDraws = 512;
  static constexpr u32 kMaxOverlays = 128;

  bool init(u32 width, u32 height) override;
  void setMeshes(const MeshTable& table) override;
  void beginFrame(Color32 clear) override;
  void setCamera(const Matrix4& view, const Matrix4& proj) override;
  void drawMesh(u32 meshIndex, const Matrix4& world, u32 sortKey) override;
  void drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) override;
  void endFrame() override;
  u32 width() const override { return w_; }
  u32 height() const override { return h_; }

 private:
  struct Draw { u32 key, mesh; Matrix4 world; };
  struct Overlay { i32 x, y, w, h; u32 color; };
  u32 w_ = 480, h_ = 272;
  MeshTable table_;
  Matrix4 view_, proj_;
  Color32 clear_;
  Draw draws_[kMaxDraws];
  u32 drawCount_ = 0;
  Overlay overlays_[kMaxOverlays];
  u32 overlayCount_ = 0;
  u32 triangles_ = 0;
};

}  // namespace rensga
