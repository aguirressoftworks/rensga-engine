// OpenGL 3.3 renderer for the Scene and Game views (MVP-48/50). Target-agnostic: it draws what gatherScene() returns.
// Vertex color x lambert from the scene's directional light, depth test, MSAA for the Scene view, an infinite-looking
// ground grid with world axes, selection outline, and gizmo-less markers for lights/cameras/empties.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "rensga/editor/viewport.h"

namespace rensga {
namespace editor {

struct RenderTarget {
  unsigned fbo = 0, tex = 0, msFbo = 0, rbColor = 0, rbDepth = 0, depthTex = 0;
  int w = 0, h = 0, samples = 0;
  void ensure(int width, int height, int msaaSamples, bool nearest);
  void destroy();
};

// A textured rectangle of the 2D layer (a sprite, or one tile). Corners in world space: top-left, top-right,
// bottom-right, bottom-left; uv in 0..1 with v down the texture. tex 0 draws a flat rectangle.
struct SpriteQuad {
  unsigned tex = 0;
  float p[4][3] = {};
  float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  float color[4] = {1, 1, 1, 1};
  unsigned id = 0;  // pick id (0 = not pickable)
};

struct RenderOptions {
  M4 view, proj;
  float eye[3] = {0, 0, 0};
  float farClip = 1000;
  bool grid = false, markers = false;
  float gridStep = 1;                         // world units between minor 3D grid lines (major = 10x)
  bool mode2d = false;                        // orthographic XY view: 2D grid, no depth on sprites
  float gridStep2d = 16, ppu = 1;             // 2D minor grid step (world units) and pixels per unit
  const std::vector<SpriteQuad>* quads = nullptr;  // sprites and tiles, already in draw order
  bool gizmos = false;                        // draw the engine Gizmos list (rensga/core/gizmos.h)
  std::vector<std::string> selection;         // every selected object (outlines); `selected` stays for the primary
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
  // ID-buffer picking: draws meshes, sprites/tiles (alpha tested) and markers with their ids into an integer target and
  // reads the rectangle [x0,x1) x [y0,y1) (pixels, y down from the top-left of a w x h view). Returns the ids found,
  // each once, the one nearest to the rectangle centre first. `ids` maps object GUIDs to ids (> 0).
  std::vector<unsigned> pick(const SceneDraw& scene, const std::vector<SpriteQuad>* quads, const std::map<std::string, unsigned>& ids,
                             const M4& view, const M4& proj, bool mode2d, int w, int h, int x0, int y0, int x1, int y1);

 private:
  struct Gpu { std::shared_ptr<const MeshData> mesh; unsigned vao = 0, vbo = 0; int count = 0; };
  const Gpu& upload(const std::shared_ptr<const MeshData>& m);
  void drawLines(const std::vector<float>& verts, const M4& vp, unsigned mode = 0x0001 /* GL_LINES */);  // x y z r g b a per vertex
  void draw2dGrid(const RenderOptions& opt, const M4& vp);
  void drawGizmoList(const M4& vp, bool mode2d);
  void drawQuads(const std::vector<SpriteQuad>& quads, const M4& vp, bool pickMode);
  void ensurePickTarget(int w, int h);
  unsigned pickFbo_ = 0, pickColor_ = 0, pickDepth_ = 0, pickW_ = 0, pickH_ = 0;
  unsigned spriteProg_ = 0, pickMeshProg_ = 0, pickSpriteProg_ = 0, spriteVao_ = 0, spriteVbo_ = 0, whiteTex_ = 0;
  unsigned triProg_ = 0, triVao_ = 0, triVbo_ = 0;
  std::shared_ptr<MeshData> markerCube_;
  unsigned prog_ = 0, gridProg_ = 0, lineProg_ = 0, gridVao_ = 0, gridVbo_ = 0, lineVao_ = 0, lineVbo_ = 0;
  std::map<const MeshData*, Gpu> gpu_;
};

}  // namespace editor
}  // namespace rensga
