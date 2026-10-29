// OpenGL 3.3 renderer for the Scene and Game views (MVP-48/50). Target-agnostic: it draws what gatherScene() returns.
// Vertex color x lambert from the scene's directional light, depth test, MSAA for the Scene view, an infinite-looking
// ground grid with world axes, selection outline, and gizmo-less markers for lights/cameras/empties.
#pragma once
#include <map>
#include <memory>
#include <string>

#include "rensga/editor/viewport.h"

namespace rensga {
namespace editor {

struct RenderTarget {
  unsigned fbo = 0, tex = 0, msFbo = 0, rbColor = 0, rbDepth = 0, depthTex = 0;
  int w = 0, h = 0, samples = 0;
  void ensure(int width, int height, int msaaSamples, bool nearest);
  void destroy();
};

struct RenderOptions {
  M4 view, proj;
  float eye[3] = {0, 0, 0};
  float farClip = 1000;
  bool grid = false, markers = false;
  float clear[4] = {0.1f, 0.1f, 0.1f, 1};
  float gridColor[4] = {1, 1, 1, 0.2f}, xColor[4] = {1, 0, 0, 1}, zColor[4] = {0, 0, 1, 1};
  float markerColor[4] = {0.8f, 0.8f, 0.8f, 1}, selectColor[4] = {1, 0.8f, 0, 1};
  std::string selected;
};

class SceneRenderer {
 public:
  bool init();  // needs a current GL 3.3 context
  void render(RenderTarget& rt, const SceneDraw& scene, const RenderOptions& opt);
  bool ready() const { return prog_ != 0; }

 private:
  struct Gpu { std::shared_ptr<const MeshData> mesh; unsigned vao = 0, vbo = 0; int count = 0; };
  const Gpu& upload(const std::shared_ptr<const MeshData>& m);
  void drawLines(const std::vector<float>& verts, const M4& vp);  // x y z r g b a per vertex
  unsigned prog_ = 0, gridProg_ = 0, lineProg_ = 0, gridVao_ = 0, gridVbo_ = 0, lineVao_ = 0, lineVbo_ = 0;
  std::map<const MeshData*, Gpu> gpu_;
};

}  // namespace editor
}  // namespace rensga
