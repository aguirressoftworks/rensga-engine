// F6.09 (B2): the CPU ParticleSystem. Pools, emission rate and bursts, shapes, gravity, over-lifetime curves, determinism, the full
// pool, sorting and drawing through renderWorld, and the screen-rectangle fallback for renderers without 3D quads.
#include "rensga/Rensga.h"
#include "rensga/core/backends.h"
#include "rensga/core/loop.h"
#include "rensga/core/render_view.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
constexpr Scalar kDt = 1.0f / 60.0f;

struct PFix {
  Arena arena;
  World world;
  PFix() {
    arena.create(2 << 20);
    WorldConfig c;
    c.maxObjects = 64;
    c.maxComponentsPerType = 32;
    world.init(c, arena);
    RenderScreen::set(480, 272);
    Particles::reset();
  }
  ~PFix() { world.shutdown(); Particles::reset(); }
  void camera() {
    GameObject* go = world.createObject("Cam");
    go->transform.setPosition(Vector3(0, 0, 10));
    go->AddComponent<Camera>();
  }
  ParticleSystem* system(const char* name, const Vector3& pos = Vector3()) {
    GameObject* go = world.createObject(name);
    go->transform.setPosition(pos);
    ParticleSystem* ps = go->AddComponent<ParticleSystem>();
    ps->playOnAwake = false;
    ps->emissionRate = 0;
    ps->startSpeed = 0;
    ps->shape = 5;  // Point
    ps->startLifetime = 1.0f;
    ps->maxParticles = 128;
    return ps;
  }
  void steps(int n) {
    for (int i = 0; i < n; ++i) Particles::fixedStep(world, kDt);
  }
};

struct CountingRenderer : NullRenderer {
  u32 rects = 0, sprites = 0;
  Rect lastRect;
  void drawRect(const Rect& r, Color32, u32) override { ++rects; lastRect = r; }
  void drawSprite(u32, const Rect&, const Rect& d, Color32, u32) override { ++sprites; lastRect = d; }
};
}  // namespace

TEST(particles_emit_live_and_die) {
  PFix f;
  ParticleSystem* ps = f.system("P");
  ps->Emit(10);
  CHECK_EQ(ps->particleCount(), 10u);
  CHECK_EQ(Particles::live(), 10u);
  CHECK_EQ(Particles::systems(), 1u);
  f.steps(30);
  CHECK_EQ(ps->particleCount(), 10u);  // lifetime 1 s
  f.steps(31);
  CHECK_EQ(ps->particleCount(), 0u);
  ps->Emit(5);
  ps->Clear();
  CHECK_EQ(ps->particleCount(), 0u);
}

TEST(particles_pool_capacity_is_fixed) {
  PFix f;
  ParticleSystem* ps = f.system("P");
  ps->maxParticles = 16;
  ps->Emit(100);
  CHECK_EQ(ps->particleCount(), 16u);
  CHECK_EQ(Particles::rejected(), 84u);
  // The whole program shares kPoolCapacity entries; a system that finds no room cannot emit but nothing crashes.
  ParticleSystem* big[6];
  for (int i = 0; i < 6; ++i) {
    big[i] = f.system("Big");
    big[i]->maxParticles = 512;
    big[i]->Emit(1);
  }
  u32 emitters = 0;
  for (int i = 0; i < 6; ++i) emitters += big[i]->particleCount();
  CHECK(emitters >= 3u && emitters < 6u);                   // 2048 - 16 leaves room for three blocks of 512
  CHECK(Particles::reserved() <= Particles::kPoolCapacity);
}

TEST(particles_emission_rate_and_bursts) {
  PFix f;
  ParticleSystem* ps = f.system("Rate");
  ps->emissionRate = 30.0f;
  ps->startLifetime = 10.0f;
  ps->loop = true;
  ps->Play();
  CHECK(ps->isPlaying());
  f.steps(120);  // 2 seconds
  CHECK(ps->particleCount() >= 59u && ps->particleCount() <= 61u);
  ps->Stop();
  const u32 before = ps->particleCount();
  f.steps(60);
  CHECK_EQ(ps->particleCount(), before);  // stopped: no new particles, the old ones stay (lifetime 10 s)

  ParticleSystem* b = f.system("Burst");
  b->loop = false;
  b->duration = 1.0f;
  b->startLifetime = 10.0f;
  b->burstTimes.Add(0.0f);
  b->burstCounts.Add(5);
  b->burstTimes.Add(0.5f);
  b->burstCounts.Add(7);
  b->Play();
  f.steps(20);
  CHECK_EQ(b->particleCount(), 5u);   // the burst at t = 0
  f.steps(20);
  CHECK_EQ(b->particleCount(), 12u);  // plus the one at 0.5 s
  f.steps(60);
  CHECK_EQ(b->particleCount(), 12u);  // not looping: duration over, nothing more
  CHECK(!b->isPlaying());
}

TEST(particles_play_on_awake_and_duration_loop) {
  PFix f;
  ParticleSystem* ps = f.system("A");
  ps->playOnAwake = true;
  ps->emissionRate = 60.0f;
  ps->startLifetime = 5.0f;
  ps->duration = 0.5f;
  ps->loop = false;
  f.steps(120);
  CHECK(!ps->isPlaying());
  CHECK(ps->particleCount() >= 29u && ps->particleCount() <= 31u);  // 0.5 s of 60/s
}

TEST(particles_gravity_and_velocity) {
  PFix f;
  ParticleSystem* ps = f.system("G", Vector3(0, 10, 0));
  ps->startLifetime = 5.0f;
  ps->gravityModifier = 1.0f;
  ps->Emit(1);
  f.steps(60);
  GameObject* go = ps->gameObject();
  (void)go;
  // y after one second of gravity (semi-implicit Euler, 1/60 s steps): -9.81 * dt^2 * n (n + 1) / 2 = -4.99 from y = 10.
  const CameraView v = makeCameraView(*([&] { f.camera(); return Camera::main(); }()), 480, 272);
  NullRenderer r;
  r.init(480, 272);
  Particles::draw(f.world, r, v, -1);
  CHECK_EQ(r.quads, 1u);
  const Vector3 c = (r.lastQuad.p[0] + r.lastQuad.p[2]) * 0.5f;
  CHECK_NEAR(c.y, 10.0f - 4.99f, 0.1f);

  PFix g;
  ParticleSystem* cone = g.system("Cone");
  cone->shape = 0;
  cone->shapeAngle = 0;
  cone->shapeRadius = 0;
  cone->startSpeed = 6.0f;
  cone->startLifetime = 5.0f;
  cone->Emit(1);
  g.steps(60);
  g.camera();
  const CameraView gv = makeCameraView(*Camera::main(), 480, 272);
  NullRenderer r2;
  r2.init(480, 272);
  Particles::draw(g.world, r2, gv, -1);
  const Vector3 c2 = (r2.lastQuad.p[0] + r2.lastQuad.p[2]) * 0.5f;
  CHECK_NEAR(c2.y, 6.0f * (60.0f * kDt), 0.05f);  // straight up at startSpeed
  CHECK_NEAR(c2.x, 0.0f, 1e-3f);
}

TEST(particles_shapes) {
  PFix f;
  f.camera();
  const CameraView v = makeCameraView(*Camera::main(), 480, 272);
  struct Case { int shape; Scalar maxR; };
  const Case cases[] = {{1, 2.0f}, {2, 2.0f}, {3, 2.0f * 1.7321f}, {4, 2.0f}};
  for (const Case& c : cases) {
    Particles::reset();
    ParticleSystem* ps = f.system("S");
    ps->shape = c.shape;
    ps->shapeRadius = 2.0f;
    ps->maxParticles = 200;
    ps->Emit(200);
    NullRenderer r;
    r.init(480, 272);
    Particles::draw(f.world, r, v, -1);
    CHECK_EQ(r.quads, 200u);
    // Every particle starts inside the shape (checked through the first quad's centre: they all fall in the radius).
    const Vector3 ctr = (r.lastQuad.p[0] + r.lastQuad.p[2]) * 0.5f;
    CHECK(ctr.magnitude() <= c.maxR + 0.01f);
    if (c.shape == 2) CHECK(ctr.y >= -0.001f);  // hemisphere: upper half only
    if (c.shape == 4) CHECK_NEAR(ctr.y, 0.0f, 1e-4f);  // circle lies in the XZ plane
    ps->Clear();
  }
}

TEST(particles_deterministic_and_seeded) {
  auto run = [](i32 seed) {
    PFix f;
    ParticleSystem* ps = f.system("D", Vector3(1, 2, 3));
    ps->shape = 1;
    ps->shapeRadius = 1.5f;
    ps->startSpeed = 3.0f;
    ps->startLifetime = 4.0f;
    ps->gravityModifier = 0.5f;
    ps->seed = seed;
    ps->Emit(40);
    f.steps(45);
    f.camera();
    const CameraView v = makeCameraView(*Camera::main(), 480, 272);
    NullRenderer r;
    r.init(480, 272);
    Particles::draw(f.world, r, v, -1);
    u32 h = 2166136261u;
    for (Scalar s : {r.lastQuad.p[0].x, r.lastQuad.p[0].y, r.lastQuad.p[0].z, r.lastQuad.p[2].x}) {
      u32 bits;
      memcpy(&bits, &s, 4);
      h = (h ^ bits) * 16777619u;
    }
    return h;
  };
  CHECK_EQ(run(7), run(7));
  CHECK(run(7) != run(8));
}

TEST(particles_over_lifetime_and_sorting) {
  PFix f;
  f.camera();
  const CameraView v = makeCameraView(*Camera::main(), 480, 272);
  ParticleSystem* ps = f.system("L", Vector3(0, 0, 0));
  ps->startSize = 2.0f;
  ps->startLifetime = 1.0f;
  ps->sizeOverLifetime = true;
  ps->sizeStart = 1.0f;
  ps->sizeEnd = 0.5f;
  ps->colorOverLifetime = true;
  ps->colorStart = Color32(255, 255, 255, 255);
  ps->colorEnd = Color32(255, 0, 0, 0);
  ps->startColor = Color32(100, 200, 255, 200);
  ps->Emit(1);
  NullRenderer r;
  r.init(480, 272);
  Particles::draw(f.world, r, v, -1);
  CHECK_NEAR((r.lastQuad.p[1] - r.lastQuad.p[0]).magnitude(), 2.0f, 1e-3f);
  CHECK(r.lastQuad.color == Color32(100, 200, 255, 200));
  f.steps(30);  // half way
  r.beginFrame(Color32());
  Particles::draw(f.world, r, v, -1);
  CHECK_NEAR((r.lastQuad.p[1] - r.lastQuad.p[0]).magnitude(), 1.5f, 0.02f);  // 2 * lerp(1, 0.5, 0.5)
  CHECK(r.lastQuad.color.a > 60 && r.lastQuad.color.a < 140);                 // 200 * lerp(255, 0) / 255
  CHECK(r.lastQuad.color.g < 150 && r.lastQuad.color.g > 80);                 // 200 * lerp(255, 0)

  // Back to front: the first quad of a batch is the farthest one.
  Particles::reset();
  ParticleSystem* s = f.system("Sorted", Vector3(0, 0, 5));
  s->startLifetime = 5.0f;
  s->Emit(1);                                       // near the camera (z = 5, camera at z = 10)
  s->gameObject()->transform.setPosition(Vector3(0, 0, -20));
  s->Emit(1);                                       // far
  s->gameObject()->transform.setPosition(Vector3(0, 0, 0));
  s->Emit(1);                                       // middle
  r.beginFrame(Color32());
  Particles::draw(f.world, r, v, -1);
  CHECK_EQ(r.quads, 3u);
  CHECK_EQ(r.quadBatches, 1u);
  CHECK_NEAR(r.lastQuad.p[0].z, -20.0f, 1e-3f);     // lastQuad is the first quad of the batch: the farthest
}

TEST(particles_destroying_the_owner_frees_its_block) {
  PFix f;
  ParticleSystem* ps = f.system("Tmp");
  ps->Emit(5);
  CHECK_EQ(Particles::systems(), 1u);
  f.world.destroy(ps->gameObject());
  f.world.flushDestroyed();
  f.steps(1);
  CHECK_EQ(Particles::systems(), 0u);
  CHECK_EQ(Particles::reserved(), 0u);
}

TEST(particles_screen_rect_fallback) {
  PFix f;
  f.camera();
  Camera::main()->orthographic = true;
  Camera::main()->orthographicSize = 136.0f;  // 1 world unit = 1 pixel on 272 pixel height
  ParticleSystem* ps = f.system("Rect", Vector3(0, 0, 0));
  ps->startSize = 20.0f;
  ps->startLifetime = 3.0f;
  ps->Emit(1);
  CountingRenderer r;
  r.init(480, 272);
  r.acceptQuads = false;
  renderWorld(f.world, r);
  CHECK_EQ(r.rects, 1u);
  CHECK_NEAR(r.lastRect.width, 20.0f, 0.1f);
  CHECK_NEAR(r.lastRect.x + r.lastRect.width * 0.5f, 240.0f, 0.1f);  // centred on the screen
  CHECK_NEAR(r.lastRect.y + r.lastRect.height * 0.5f, 136.0f, 0.1f);
}

TEST(particles_hidden_by_layer_or_inactive_owner) {
  PFix f;
  f.camera();
  Camera::main()->cullingMask = static_cast<i32>(~(1u << 6));
  ParticleSystem* ps = f.system("Layered");
  ps->startLifetime = 3.0f;
  ps->gameObject()->layer = 6;
  ps->Emit(3);
  NullRenderer r;
  r.init(480, 272);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);
  ps->gameObject()->layer = 0;
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 3u);
  ps->gameObject()->SetActive(false);
  renderWorld(f.world, r);
  CHECK_EQ(r.quads, 0u);
}
