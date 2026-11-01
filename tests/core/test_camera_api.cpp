// F6.09 (B2): the Camera API against known matrices, and renderWorld with several cameras (NullRenderer call counts).
#include "rensga/Rensga.h"
#include "rensga/core/backends.h"
#include "rensga/core/loop.h"
#include "rensga/core/render_view.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct CamFix {
  Arena arena;
  World world;
  CamFix() {
    arena.create(2 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    world.init(c, arena);
    RenderScreen::set(480, 272);
    Debug::clearLines();
    Particles::reset();
  }
  ~CamFix() { world.shutdown(); }
  Camera* camera(const char* name, const Vector3& pos, Scalar depth = 0) {
    GameObject* go = world.createObject(name);
    go->transform.setPosition(pos);
    Camera* c = go->AddComponent<Camera>();
    c->depth = depth;
    return c;
  }
};
Scalar halfWidthAt10() { return 10.0f * Mathf::Tan(30.0f * Mathf::Deg2Rad) * 480.0f / 272.0f; }
bool near3(const Vector3& a, const Vector3& b, Scalar e = 1e-3f) { return (a - b).magnitude() < e; }
}  // namespace

TEST(camera_main_is_lowest_depth) {
  CamFix f;
  CHECK(Camera::main() == nullptr);
  Camera* a = f.camera("A", Vector3(0, 0, 10), 5);
  Camera* b = f.camera("B", Vector3(0, 0, 10), -1);
  Camera* c = f.camera("C", Vector3(0, 0, 10), 2);
  CHECK(Camera::main() == b);
  b->setEnabled(false);
  CHECK(Camera::main() == c);
  c->gameObject()->SetActive(false);
  CHECK(Camera::main() == a);
}

TEST(camera_perspective_known_matrices) {
  CamFix f;
  Camera* cam = f.camera("Cam", Vector3(0, 0, 10));  // looks down -Z, fov 60, aspect 480/272
  // The centre of the screen sees the origin 10 units ahead.
  CHECK(near3(cam->WorldToScreenPoint(Vector3(0, 0, 0)), Vector3(240, 136, 10)));
  CHECK(near3(cam->ScreenToWorldPoint(Vector3(240, 136, 10)), Vector3(0, 0, 0)));
  // The right edge at distance 10 is halfWidth away; the top edge is tan(30) * 10.
  const Scalar hw = halfWidthAt10(), hh = 10.0f * Mathf::Tan(30.0f * Mathf::Deg2Rad);
  CHECK(near3(cam->WorldToScreenPoint(Vector3(hw, 0, 0)), Vector3(480, 136, 10), 5e-3f));
  CHECK(near3(cam->WorldToScreenPoint(Vector3(0, hh, 0)), Vector3(240, 0, 10), 5e-3f));   // screen y runs down
  CHECK(near3(cam->WorldToScreenPoint(Vector3(-hw, -hh, 0)), Vector3(0, 272, 10), 5e-3f));
  CHECK(near3(cam->ScreenToWorldPoint(Vector3(480, 0, 10)), Vector3(hw, hh, 0), 5e-3f));
  // z is the distance along the view direction, not along the ray: a point twice as far is twice as far from the camera plane.
  CHECK(near3(cam->ScreenToWorldPoint(Vector3(480, 0, 20)), Vector3(2 * hw, 2 * hh, -10), 1e-2f));
  // Round trip for an arbitrary point.
  const Vector3 p(3.0f, -2.0f, 4.0f);
  CHECK(near3(cam->ScreenToWorldPoint(cam->WorldToScreenPoint(p)), p, 2e-3f));
  // Viewport coordinates: 0..1, origin bottom-left.
  CHECK(near3(cam->WorldToViewportPoint(Vector3(hw, hh, 0)), Vector3(1, 1, 10), 5e-3f));
  CHECK(near3(cam->WorldToViewportPoint(Vector3(-hw, -hh, 0)), Vector3(0, 0, 10), 5e-3f));
  CHECK(near3(cam->ViewportToWorldPoint(Vector3(0.5f, 0.5f, 10)), Vector3(0, 0, 0)));
  // Behind the camera the distance is negative.
  CHECK(cam->WorldToScreenPoint(Vector3(0, 0, 20)).z < 0);
}

TEST(camera_rays) {
  CamFix f;
  Camera* cam = f.camera("Cam", Vector3(1, 2, 10));
  const Ray center = cam->ScreenPointToRay(Vector2(240, 136));
  CHECK(near3(center.origin, Vector3(1, 2, 10)));
  CHECK(near3(center.direction, Vector3(0, 0, -1)));
  // A ray through a screen point passes through the world point that ScreenToWorldPoint returns.
  const Vector2 sp(100, 50);
  const Ray r = cam->ScreenPointToRay(sp);
  const Vector3 onPlane = cam->ScreenToWorldPoint(Vector3(sp.x, sp.y, 10));
  CHECK_NEAR(r.direction.magnitude(), 1.0f, 1e-4f);
  CHECK(near3(r.GetPoint((onPlane - r.origin).magnitude()), onPlane, 2e-3f));
  const Ray v = cam->ViewportPointToRay(Vector2(0.5f, 0.5f));
  CHECK(near3(v.direction, Vector3(0, 0, -1)));
  // A turned camera: yaw 90 degrees about +Y makes the camera look down -X.
  cam->transform()->setRotation(Quaternion::AngleAxis(90.0f, Vector3(0, 1, 0)));
  CHECK(near3(cam->ScreenPointToRay(Vector2(240, 136)).direction, Vector3(-1, 0, 0), 1e-3f));
}

TEST(camera_orthographic) {
  CamFix f;
  Camera* cam = f.camera("Cam", Vector3(0, 0, 10));
  cam->orthographic = true;
  cam->orthographicSize = 5.0f;
  const Scalar hw = 5.0f * 480.0f / 272.0f;
  CHECK(near3(cam->WorldToScreenPoint(Vector3(hw, 5, 0)), Vector3(480, 0, 10), 5e-3f));
  CHECK(near3(cam->WorldToScreenPoint(Vector3(-hw, -5, 3)), Vector3(0, 272, 7), 5e-3f));  // z = distance along the view direction
  // No perspective: the screen position does not depend on the depth.
  CHECK(near3(cam->WorldToScreenPoint(Vector3(2, 1, -50)), Vector3(cam->WorldToScreenPoint(Vector3(2, 1, 0)).x, cam->WorldToScreenPoint(Vector3(2, 1, 0)).y, 60), 1e-3f));
  CHECK(near3(cam->ScreenToWorldPoint(Vector3(480, 0, 10)), Vector3(hw, 5, 0), 5e-3f));
  CHECK(near3(cam->ScreenToWorldPoint(Vector3(480, 0, 25)), Vector3(hw, 5, -15), 5e-3f));
  const Ray r = cam->ScreenPointToRay(Vector2(480, 0));
  CHECK(near3(r.direction, Vector3(0, 0, -1), 1e-3f));  // parallel rays
  CHECK_NEAR(r.origin.x, hw, 5e-3f);
  CHECK_NEAR(r.origin.y, 5.0f, 5e-3f);
}

TEST(camera_viewport_rect) {
  CamFix f;
  Camera* cam = f.camera("Cam", Vector3(0, 0, 10));
  cam->viewportX = 0.5f;  // right half of the screen
  cam->viewportW = 0.5f;
  const CameraView v = makeCameraView(*cam, 480, 272);
  CHECK(!v.full);
  CHECK_EQ(v.x, 240);
  CHECK_EQ(v.w, 240);
  CHECK_EQ(v.y, 0);
  CHECK_EQ(v.h, 272);
  // The aspect follows the rectangle (240 x 272), so the half width at distance 10 is smaller.
  const Scalar hh = 10.0f * Mathf::Tan(30.0f * Mathf::Deg2Rad), hw = hh * 240.0f / 272.0f;
  CHECK(near3(cam->WorldToScreenPoint(Vector3(0, 0, 0)), Vector3(360, 136, 10)));
  CHECK(near3(cam->WorldToScreenPoint(Vector3(hw, 0, 0)), Vector3(480, 136, 10), 5e-3f));
  CHECK(near3(cam->WorldToViewportPoint(Vector3(0, 0, 0)), Vector3(0.5f, 0.5f, 10)));
  CHECK(near3(cam->ScreenToWorldPoint(Vector3(360, 136, 10)), Vector3(0, 0, 0)));
  // A bottom-left quarter: viewport y counts from the bottom, screen y from the top.
  Camera* q = f.camera("Quarter", Vector3(0, 0, 10), 1);
  q->viewportW = 0.5f;
  q->viewportH = 0.5f;
  const CameraView qv = makeCameraView(*q, 480, 272);
  CHECK_EQ(qv.x, 0);
  CHECK_EQ(qv.y, 136);
  CHECK_EQ(qv.w, 240);
  CHECK_EQ(qv.h, 136);
}

TEST(camera_default_view_matches_the_old_renderer) {
  CamFix f;
  Camera* cam = f.camera("Cam", Vector3(1, 2, 3));
  const CameraView v = makeCameraView(*cam, 480, 272);
  CHECK(v.full);
  const Matrix4 view = cam->transform()->localToWorld().inverse();
  const Matrix4 proj = Matrix4::perspective(cam->fov, static_cast<Scalar>(480) / static_cast<Scalar>(272), cam->nearClip, cam->farClip);
  bool same = true;
  for (int i = 0; i < 16; ++i) same = same && v.view.m[i] == view.m[i] && v.proj.m[i] == proj.m[i];
  CHECK(same);
}

TEST(camera_render_world_single_default_camera_makes_the_old_calls) {
  CamFix f;
  f.camera("Cam", Vector3(0, 0, 10));
  NullRenderer r;
  r.init(480, 272);
  renderWorld(f.world, r, Color32(10, 20, 30, 255));
  CHECK(r.cameraSet);
  CHECK(r.frameClear == Color32(10, 20, 30, 255));  // a default background keeps the caller's clear color
  CHECK_EQ(r.viewportCalls, 0u);
  CHECK_EQ(r.clearViews, 0u);
}

TEST(camera_render_world_depth_order_viewport_and_clears) {
  CamFix f;
  Camera* ui = f.camera("Mini", Vector3(0, 20, 0), 10);
  ui->orthographic = true;
  ui->viewportX = 0.75f;
  ui->viewportY = 0.5f;
  ui->viewportW = 0.25f;
  ui->viewportH = 0.5f;
  ui->clearFlags = 2;  // DepthOnly
  Camera* main = f.camera("Main", Vector3(0, 0, 10), -5);
  main->backgroundColor = Color32(1, 2, 3, 255);
  NullRenderer r;
  r.init(480, 272);
  renderWorld(f.world, r);
  CHECK(r.frameClear == Color32(1, 2, 3, 255));  // the lowest depth draws first and owns the frame clear
  CHECK_EQ(r.viewportCalls, 2u);                 // the mini map rectangle, then the whole screen again for the 2D pass
  CHECK_EQ(r.clearViews, 1u);
  CHECK_EQ(r.lastClearFlags, kClearDepth);
  CHECK_EQ(r.viewport[0], 0);
  CHECK_EQ(r.viewport[2], 480);
  ui->clearFlags = 1;
  renderWorld(f.world, r);
  CHECK_EQ(r.lastClearFlags, kClearColor | kClearDepth);
  ui->clearFlags = 3;  // Nothing
  renderWorld(f.world, r);
  CHECK_EQ(r.clearViews, 0u);
}

TEST(camera_culling_mask_by_layer) {
  CamFix f;
  Camera* cam = f.camera("Cam", Vector3(0, 0, 10));
  GameObject* a = f.world.createObject("LineA");
  a->layer = 3;
  LineRenderer* la = a->AddComponent<LineRenderer>();
  la->SetPosition(0, Vector3(-1, 0, 0));
  la->SetPosition(1, Vector3(1, 0, 0));
  GameObject* b = f.world.createObject("LineB");
  b->layer = 4;
  LineRenderer* lb = b->AddComponent<LineRenderer>();
  lb->SetPosition(0, Vector3(-1, 1, 0));
  lb->SetPosition(1, Vector3(1, 1, 0));
  NullRenderer r;
  r.init(480, 272);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 2u);  // mask -1: every layer
  cam->cullingMask = static_cast<i32>(~(1u << 3));
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 1u);  // layer 3 hidden
  cam->cullingMask = 1 << 4;
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 1u);
  cam->cullingMask = 1;
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);
  CHECK(layerInMask(-1, 31));
  CHECK(!layerInMask(0, 0));
}

TEST(input_pointer_position_and_edges) {
  VirtualGamepad pad;
  Input::update(pad);
  CHECK(!Input::mousePresent());
  CHECK(!Input::GetMouseButton(0));
  pad.pointerActive = true;
  pad.pointerX = 120;
  pad.pointerY = 80;
  Input::update(pad);
  CHECK(Input::mousePresent());
  CHECK(Input::mousePosition().x == 120 && Input::mousePosition().y == 80);
  CHECK(!Input::GetMouseButton(0) && !Input::GetMouseButtonDown(0));
  pad.pointerDown = true;
  Input::update(pad);
  CHECK(Input::GetMouseButton(0) && Input::GetMouseButtonDown(0) && !Input::GetMouseButtonUp(0));
  Input::update(pad);
  CHECK(Input::GetMouseButton(0) && !Input::GetMouseButtonDown(0));  // held, no new edge
  pad.pointerDown = false;
  Input::update(pad);
  CHECK(!Input::GetMouseButton(0) && Input::GetMouseButtonUp(0));
  CHECK(!Input::GetMouseButton(1));  // only button 0
  pad.pointerActive = false;
  Input::update(pad);
  CHECK(Input::mousePosition().x == 0);
}

#include "rensga/core/lighting.h"
TEST(point_light_falloff_is_the_curve_the_ge_can_draw) {
  // GL and GU share kPointLightK: 1 / (1 + K (d / range)^2). The GU sets sceGuLightAtt(1, 0, K / range^2), which is the same thing.
  CHECK_NEAR(pointLightFalloff(0.0f, 8.0f), 1.0f, 1e-6f);
  CHECK_NEAR(pointLightFalloff(8.0f, 8.0f), 1.0f / 16.0f, 1e-6f);
  CHECK_NEAR(pointLightFalloff(0.258199f * 8.0f, 8.0f), 0.5f, 1e-3f);
  CHECK_NEAR(pointLightFalloff(0.447214f * 8.0f, 8.0f), 0.25f, 1e-3f);
  CHECK(pointLightFalloff(2.0f, 8.0f) > pointLightFalloff(3.0f, 8.0f));
  CHECK_NEAR(pointLightFalloff(1.0f, 0.0f), 0.0f, 1e-6f);
  // The GE form 1 / (c + l d + q d^2) with c = 1, l = 0, q = K / range^2 gives the same number.
  const Scalar q = kPointLightK / (8.0f * 8.0f), d = 5.0f;
  CHECK_NEAR(1.0f / (1.0f + 0.0f * d + q * d * d), pointLightFalloff(d, 8.0f), 1e-6f);
}
