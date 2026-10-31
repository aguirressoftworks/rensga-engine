// F5.14 runtime: frustum extraction, the sphere test, and renderWorld culling in NullRenderer draw counts.
#include "fixtures/pack3d_builder.h"
#include "rensga/core/backends.h"
#include "rensga/core/components.h"
#include "rensga/core/cull.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;
using rensga::fixtures::Pack3dBuilder;
using rensga::fixtures::PackImage;

TEST(cull_frustum_planes_and_spheres) {
  // Camera at the origin looking down -Z (view = identity), 90 degree fov, square aspect, near 1, far 100.
  Matrix4 proj = Matrix4::perspective(90.0f, 1.0f, 1.0f, 100.0f);
  Frustum f = frustumFromViewProj(proj);
  CHECK(f.valid);
  for (int p = 0; p < 6; ++p) {
    Scalar len = f.planes[p][0] * f.planes[p][0] + f.planes[p][1] * f.planes[p][1] + f.planes[p][2] * f.planes[p][2];
    CHECK_NEAR(len, 1.0f, 1e-5f);
  }
  CHECK(sphereInFrustum(f, Vector3(0, 0, -10), 0.5f));
  CHECK(sphereInFrustum(f, Vector3(9, 0, -10), 0.5f));    // inside the 45 degree half-angle
  CHECK(!sphereInFrustum(f, Vector3(20, 0, -10), 0.5f));  // far to the right
  CHECK(!sphereInFrustum(f, Vector3(-20, 0, -10), 0.5f));
  CHECK(!sphereInFrustum(f, Vector3(0, 20, -10), 0.5f));
  CHECK(!sphereInFrustum(f, Vector3(0, -20, -10), 0.5f));
  CHECK(!sphereInFrustum(f, Vector3(0, 0, 10), 0.5f));    // behind
  CHECK(!sphereInFrustum(f, Vector3(0, 0, -150), 0.5f));  // beyond far
  CHECK(sphereInFrustum(f, Vector3(0, 0, -100.4f), 1.0f)); // the radius reaches back in
  CHECK(sphereInFrustum(f, Vector3(11, 0, -10), 2.0f));   // straddles the right plane
  CHECK(sphereInFrustum(Frustum(), Vector3(1e6f, 0, 0), 0.1f));  // an invalid frustum never culls
}

TEST(cull_degenerate_matrix_never_culls) {
  Matrix4 z = Matrix4::identity();
  for (int i = 0; i < 16; ++i) z.m[i] = 0;
  Frustum f = frustumFromViewProj(z);
  CHECK(!f.valid);
  CHECK(sphereInFrustum(f, Vector3(1e5f, 1e5f, 1e5f), 1.0f));
}

namespace {
struct CullFixture {
  Pack3dBuilder b;
  PackImage img;
  PackView v;
  PackAssets assets;
  Arena arena;
  World world;
  LoadedPack lp;
  u32 handles[1] = {1};
  CullFixture() {
    b.build(img);
    v.open(img.data, img.size);
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
    world.loadPack(v, &lp);
    assets = v.assets();
    GfxAssets::bind(&v, &assets, handles, 1);
  }
  ~CullFixture() { GfxAssets::unbind(); }
};
}  // namespace

TEST(cull_render_world_counts) {
  CullFixture f;
  NullRenderer r;
  r.init(480, 272);
  r.setMeshes(f.lp.meshes);
  // The camera sits at (0,1,6) looking down -Z; the cube (-1.5,0,0) and the bar (1.5,0,0) are both in view.
  renderWorld(f.world, r);
  CHECK_EQ(r.draws, 2u);
  GameObject* cube = f.world.findByName("Cube");
  GameObject* bar = f.world.findByName("Bar");
  cube->transform.setPosition(Vector3(-1.5f, 0, 60));     // behind the camera
  renderWorld(f.world, r);
  CHECK_EQ(r.draws, 1u);
  bar->transform.setPosition(Vector3(500, 0, 0));          // far to the side
  renderWorld(f.world, r);
  CHECK_EQ(r.draws, 0u);
  cube->transform.setPosition(Vector3(0, 0, -250));        // beyond the far plane (200)
  renderWorld(f.world, r);
  CHECK_EQ(r.draws, 0u);
  cube->transform.setPosition(Vector3(0, 0, -150));
  renderWorld(f.world, r);
  CHECK_EQ(r.draws, 1u);
  CHECK_EQ(r.exDraws, 4u);  // cumulative over the five frames: 2 + 1 + 0 + 0 + 1; every draw is a lit drawMeshEx
}
