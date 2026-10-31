// F5.14 runtime: light gathering, per-object selection order, RenderSettings, and renderWorld handing lights to the renderer.
#include "fixtures/pack3d_builder.h"
#include "rensga/core/backends.h"
#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/lighting.h"
#include "rensga/core/loop.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;
using rensga::fixtures::Pack3dBuilder;
using rensga::fixtures::PackImage;

namespace {
struct LightWorld {
  Arena arena;
  World world;
  LightWorld() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 64;
    c.maxComponentsPerType = 32;
    c.spareComponents = 32;
    world.init(c, arena);
  }
  Light* addPoint(const char* name, Scalar x, Scalar y, Scalar z, Scalar intensity = 1, Scalar range = 20, i32 mode = 0) {
    GameObject* go = world.createObject(name);
    go->transform.setPosition(Vector3(x, y, z));
    Light* l = go->AddComponent<Light>();
    l->type = 1;
    l->intensity = intensity;
    l->range = range;
    l->renderMode = mode;
    return l;
  }
};
}  // namespace

TEST(lighting_gather_and_settings) {
  LightWorld w;
  w.addPoint("p0", 1, 2, 3, 0.5f, 7);
  GameObject* sun = w.world.createObject("sun");
  Light* d = sun->AddComponent<Light>();
  d->type = 0;
  d->color = Color32(10, 20, 30, 255);
  sun->transform.setRotation(Quaternion::AngleAxis(90, Vector3(1, 0, 0)));  // forward (0,0,1) -> (0,-1,0)
  GameObject* off = w.world.createObject("off");
  off->AddComponent<Light>();
  off->SetActive(false);
  GameObject* rs = w.world.createObject("rs");
  RenderSettings* r = rs->AddComponent<RenderSettings>();
  r->ambient = Color32(1, 2, 3, 255);
  r->fogEnabled = true;
  r->fogStart = 5;
  r->fogEnd = 50;
  LightSet set;
  gatherLights(w.world, set);
  CHECK_EQ(set.count, 2u);  // the inactive light is skipped
  CHECK_EQ(set.lights[0].type, 1);
  CHECK_NEAR(set.lights[0].vec.y, 2.0f, 1e-5f);
  CHECK_NEAR(set.lights[0].range, 7.0f, 1e-5f);
  CHECK_EQ(set.lights[1].type, 0);
  CHECK_NEAR(set.lights[1].vec.y, -1.0f, 1e-4f);
  CHECK_EQ(set.lights[1].color.g, 20);
  CHECK(set.fogOn);
  CHECK_NEAR(set.fogEnd, 50.0f, 1e-5f);
  CHECK_EQ(set.ambient.b, 3);
}

TEST(lighting_defaults_without_render_settings) {
  LightWorld w;
  LightSet set;
  gatherLights(w.world, set);
  CHECK_EQ(set.count, 0u);
  CHECK(!set.fogOn);
  CHECK_EQ(set.ambient.r, 0);
}

TEST(lighting_directional_compat) {
  LightWorld w;
  GameObject* go = w.world.createObject("old");
  go->AddComponent<DirectionalLight>();
  LightSet set;
  gatherLights(w.world, set);
  CHECK_EQ(set.count, 1u);
  CHECK_EQ(set.lights[0].type, 0);
  // With a real Light in the scene the old component is ignored.
  w.addPoint("p", 0, 0, 0);
  gatherLights(w.world, set);
  CHECK_EQ(set.count, 1u);
  CHECK_EQ(set.lights[0].type, 1);
}

// 12 point lights on a line, equal intensity and range: the object at x = 0 gets the four nearest, nearest first.
TEST(lighting_twelve_lights_top_four_order) {
  LightWorld w;
  for (int i = 0; i < 12; ++i) {
    char n[8];
    snprintf(n, sizeof n, "p%d", i);
    w.addPoint(n, static_cast<Scalar>(i), 0, 0);
  }
  LightSet set;
  gatherLights(w.world, set);
  CHECK_EQ(set.count, 12u);
  u8 idx[8];
  u32 n = selectLights(set, Vector3(0, 0, 0), 0.5f, 4, idx);
  CHECK_EQ(n, 4u);
  CHECK_EQ(idx[0], 0);
  CHECK_EQ(idx[1], 1);
  CHECK_EQ(idx[2], 2);
  CHECK_EQ(idx[3], 3);
  n = selectLights(set, Vector3(11, 0, 0), 0.5f, 4, idx);
  CHECK_EQ(n, 4u);
  CHECK_EQ(idx[0], 11);
  CHECK_EQ(idx[1], 10);
  CHECK_EQ(idx[2], 9);
  CHECK_EQ(idx[3], 8);
  // Equidistant lights (x = 4 sees 3 and 5): the lower component index wins the tie.
  n = selectLights(set, Vector3(4, 0, 0), 0.0f, 3, idx);
  CHECK_EQ(n, 3u);
  CHECK_EQ(idx[0], 4);
  CHECK_EQ(idx[1], 3);
  CHECK_EQ(idx[2], 5);
  // The count is capped at 8 whatever the caller asks for.
  CHECK_EQ(selectLights(set, Vector3(4, 0, 0), 0.0f, 20, idx), 8u);
}

TEST(lighting_directional_and_important_first_and_zero_skipped) {
  LightWorld w;
  w.addPoint("near", 0, 0, 0, 1);                  // 0
  w.addPoint("far", 100, 0, 0, 5, 10);             // 1: out of range at the object -> score 0, skipped
  w.addPoint("impFar", 8, 0, 0, 0.1f, 20, LightRenderMode::Important);  // 2: weak but Important
  GameObject* sun = w.world.createObject("sun");   // 3
  sun->AddComponent<Light>()->type = 0;
  w.addPoint("mid", 3, 0, 0, 1);                   // 4
  LightSet set;
  gatherLights(w.world, set);
  u8 idx[8];
  u32 n = selectLights(set, Vector3(0, 0, 0), 0.0f, 4, idx);
  CHECK_EQ(n, 4u);
  CHECK_EQ(idx[0], 2);  // forced group in component order: Important point (2), then the directional (3)
  CHECK_EQ(idx[1], 3);
  CHECK_EQ(idx[2], 0);  // then by score: near (1.0) before mid
  CHECK_EQ(idx[3], 4);
  n = selectLights(set, Vector3(0, 0, 0), 0.0f, 8, idx);
  CHECK_EQ(n, 4u);      // the out-of-range light never appears
  // Attenuation: sphere radius counts as distance already covered.
  CHECK_EQ(selectLights(set, Vector3(100, 0, 0), 0.0f, 8, idx), 3u);  // far (1), Important (2), sun (3); the others are out of range
}

// renderWorld hands the frame's lights to the renderer once and gives each lit object at most N of them.
TEST(lighting_render_world_limits_each_object_to_four) {
  Pack3dBuilder b;
  PackImage img;
  b.build(img);
  PackView v;
  CHECK(v.open(img.data, img.size));
  LightWorld w;
  LoadedPack lp;
  CHECK(w.world.loadPack(v, &lp));
  PackAssets assets = v.assets();
  u32 handles[1] = {7};
  GfxAssets::bind(&v, &assets, handles, 1);
  for (int i = 0; i < 12; ++i) {
    char n[8];
    snprintf(n, sizeof n, "pl%d", i);
    w.addPoint(n, -1.5f + static_cast<Scalar>(i) * 0.1f, 0.5f, 1, 1, 10);
  }
  NullRenderer r;
  r.init(480, 272);
  r.setMeshes(lp.meshes);
  renderWorld(w.world, r);
  CHECK_EQ(r.lightCount, 13u);  // 12 points + the Sun
  CHECK_EQ(r.exDraws, 2u);
  CHECK_EQ(r.lastParams.lightCount, 4);
  CHECK_EQ(r.lastParams.texture, 7u);
  GfxAssets::unbind();
}
