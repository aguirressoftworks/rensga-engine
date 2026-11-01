// F6.09 (B2): LineRenderer ribbons and Debug::DrawLine in renderWorld (NullRenderer quad counts), plus the ribbon geometry.
#include "rensga/Rensga.h"
#include "rensga/core/backends.h"
#include "rensga/core/loop.h"
#include "rensga/core/render_view.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct LineFix {
  Arena arena;
  World world;
  Camera* cam = nullptr;
  LineFix() {
    arena.create(2 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    world.init(c, arena);
    RenderScreen::set(480, 272);
    Debug::clearLines();
    setDebugDraw(true);
    GameObject* go = world.createObject("Cam");
    go->transform.setPosition(Vector3(0, 0, 10));
    cam = go->AddComponent<Camera>();
  }
  ~LineFix() { world.shutdown(); Debug::clearLines(); }
  LineRenderer* line(const char* name, const Vector3& pos, u32 points) {
    GameObject* go = world.createObject(name);
    go->transform.setPosition(pos);
    LineRenderer* lr = go->AddComponent<LineRenderer>();
    for (u32 i = 0; i < points; ++i) lr->SetPosition(i, Vector3(static_cast<Scalar>(i), static_cast<Scalar>(i & 1), 0));
    return lr;
  }
};
}  // namespace

TEST(linerenderer_segment_count_and_loop) {
  LineFix f;
  NullRenderer r;
  r.init(480, 272);
  LineRenderer* lr = f.line("L", Vector3(), 4);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 3u);  // 4 points, 3 segments
  CHECK_EQ(Ribbons::quadsDrawn(), 3u);
  lr->loop = true;
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 4u);  // closes the polyline
  lr->SetPositionCount(1);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);  // a single point draws nothing
  lr->SetPositionCount(4);
  lr->setEnabled(false);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);
}

TEST(linerenderer_width_color_and_local_space) {
  LineFix f;
  NullRenderer r;
  r.init(480, 272);
  LineRenderer* lr = f.line("L", Vector3(5, 0, 0), 2);
  lr->width = 0.5f;
  lr->color = Color32(200, 100, 50, 255);
  lr->useWorldSpace = false;  // positions relative to the object at (5, 0, 0)
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 1u);
  const Quad3D q = r.lastQuad;
  CHECK(q.color == Color32(200, 100, 50, 255));
  CHECK_NEAR((q.p[0] - q.p[3]).magnitude(), 0.5f, 1e-3f);  // the ribbon is `width` wide
  CHECK_NEAR((q.p[0] - q.p[1]).magnitude(), Vector3(1, 1, 0).magnitude(), 1e-3f);  // and as long as the segment
  CHECK_NEAR(q.p[3].x, 5.0f, 0.5f);  // starts at the object position (5, 0, 0)
  lr->useWorldSpace = true;
  renderWorld(f.world, r);
  CHECK_NEAR(r.lastQuad.p[3].x, 0.0f, 0.5f);
}

TEST(linerenderer_ribbon_faces_the_camera) {
  LineFix f;
  const CameraView v = makeCameraView(*f.cam, 480, 272);
  Quad3D q;
  Ribbons::segmentQuad(v, Vector3(-2, -1, 0), Vector3(3, 2, 0), 0.4f, Color32(255, 255, 255, 255), q);
  const Vector3 e1 = q.p[1] - q.p[0], e2 = q.p[3] - q.p[0];
  const Vector3 n = Vector3::Cross(e1, e2).normalized();
  CHECK_NEAR(Mathf::Abs(Vector3::Dot(n, v.forward)), 1.0f, 1e-3f);  // the normal points at the camera
  // A segment along the view direction cannot be turned to face us; it still produces a finite quad.
  Ribbons::segmentQuad(v, Vector3(0, 0, 0), Vector3(0, 0, -5), 0.2f, Color32(255, 255, 255, 255), q);
  CHECK(q.p[0].magnitude() < 1000.0f);
}

TEST(debug_drawline_is_drawn_in_dev_builds) {
  LineFix f;
  NullRenderer r;
  r.init(480, 272);
  Debug::DrawLine(Vector3(0, 0, 0), Vector3(1, 1, 0), Color32(255, 0, 0, 255));
  Debug::DrawLine(Vector3(0, 0, 0), Vector3(-1, 1, 0), Color32(0, 255, 0, 255), 5.0f);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 2u);
  // One-frame lines vanish when the next frame starts, timed lines stay for their duration.
  Debug::tickLines(0.016f);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 1u);
  CHECK(r.lastQuad.color == Color32(0, 255, 0, 255));
  setDebugDraw(false);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);
  setDebugDraw(true);
  CHECK(debugDrawEnabled());
  Debug::tickLines(6.0f);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);
}

TEST(debug_lines_without_a_3d_renderer_fall_back_without_crashing) {
  LineFix f;
  NullRenderer r;
  r.init(480, 272);
  r.acceptQuads = false;  // GS-like: ribbons are rotated, so they are skipped; nothing is submitted
  f.line("L", Vector3(), 3);
  Debug::DrawLine(Vector3(0, 0, 0), Vector3(1, 0, 0));
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);
}
