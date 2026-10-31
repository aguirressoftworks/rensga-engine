// F4.08 part 1: Rigidbody2D (dynamic, kinematic, static) on a hand-built tilemap, stepped by Rigidbody2DSim::fixedStep.
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga/core/time.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

constexpr u32 kW = 20, kH = 10, kTs = 16;
constexpr Scalar kDt = 1.0f / 60.0f;
enum : u16 { kEmpty = 0xFFFF, kSolid = 1, kOneWay = 2 };

struct BodyFixture {
  Arena arena;
  World world;
  u16 tiles[kW * kH];
  u8 flags[4];
  alignas(16) u8 blob[kW * kH * 2 + 16];
  PackTilemapRec rec = {};
  PackAssets assets;

  BodyFixture() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    world.init(c, arena);
    for (u16& t : tiles) t = kEmpty;
    memset(flags, 0, sizeof flags);
    flags[kSolid] = PackTileFlag::kSolid;
    flags[kOneWay] = PackTileFlag::kOneWay;
    TimeInternal::reset(kDt);
    TimeInternal::beginFixedStep(kDt);
    Rigidbody2DSim::setGravity(Rigidbody2DSim::kDefaultGravity);
  }
  ~BodyFixture() { GfxAssets::unbind(); }
  void set(u32 x, u32 y, u16 t) { tiles[y * kW + x] = t; }
  void floor() {  // rows 8 and 9 -> surface at y = 128
    for (u32 x = 0; x < kW; ++x) { set(x, 8, kSolid); set(x, 9, kSolid); }
  }
  void bind() {
    memcpy(blob, tiles, sizeof tiles);
    memcpy(blob + sizeof tiles, flags, sizeof flags);
    Guid::parse("00000000-0000-4000-8000-0000000000b1", rec.guid);
    rec.width = kW; rec.height = kH; rec.tileSize = kTs;
    rec.tilesOffset = 0;
    rec.solidOffset = sizeof tiles;
    rec.solidCount = 4;
    assets.blob = blob;
    assets.blobSize = sizeof blob;
    assets.tilemaps = &rec;
    assets.tilemapCount = 1;
    GfxAssets::bind(nullptr, &assets, nullptr, 0);
    GameObject* map = world.createObject("map");
    map->AddComponent<Tilemap>()->map = rec.guid;
    map->AddComponent<TilemapCollider2D>();
  }
  Rigidbody2D* body(i32 type, Scalar x, Scalar y, bool collider = true) {
    GameObject* go = world.createObject("body");
    go->transform.setPosition(Vector3(x, y, 0));
    if (collider) {
      BoxCollider2D* b = go->AddComponent<BoxCollider2D>();
      b->sizeX = 12;
      b->sizeY = 14;
    }
    Rigidbody2D* rb = go->AddComponent<Rigidbody2D>();
    rb->bodyType = type;
    return rb;
  }
  void step(int n) {
    for (int i = 0; i < n; ++i) Rigidbody2DSim::fixedStep(world);
  }
};

Scalar posX(Rigidbody2D* rb) { return rb->transform()->position().x; }
Scalar posY(Rigidbody2D* rb) { return rb->transform()->position().y; }

}  // namespace

TEST(rb2d_dynamic_falls_and_rests_on_tilemap) {
  BodyFixture f;
  f.floor();
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Dynamic, 40, 20);
  BoxCollider2D* box = rb->gameObject()->GetComponent<BoxCollider2D>();
  f.step(2);
  CHECK(posY(rb) > 20.0f);          // gravity moves it down
  CHECK(rb->velocityY > 0.0f);
  f.step(120);                      // 2 s: far more than the 108 px fall needs
  CHECK_NEAR(posY(rb) + 14.0f, 128.0f, 0.01f);  // standing on the floor surface
  CHECK(box->grounded);
  CHECK_EQ(rb->velocityY, 0.0f);    // the landing removed the speed
  CHECK_NEAR(posX(rb), 40.0f, 0.001f);
  const Scalar y = posY(rb);
  f.step(300);                      // resting is stable: no sinking, no jitter
  CHECK_EQ(posY(rb), y);
  CHECK(box->grounded);
  CHECK_EQ(rb->velocityY, 0.0f);
}

TEST(rb2d_gravity_scale_and_world_gravity) {
  BodyFixture f;
  f.bind();  // no floor: free fall
  Rigidbody2D* a = f.body(RigidbodyType::Dynamic, 20, 0);
  Rigidbody2D* b = f.body(RigidbodyType::Dynamic, 60, 0);
  Rigidbody2D* z = f.body(RigidbodyType::Dynamic, 100, 0);
  b->gravityScale = 2.0f;
  z->gravityScale = 0.0f;
  f.step(10);
  CHECK(posY(b) > posY(a) * 1.9f);
  CHECK(posY(b) < posY(a) * 2.1f);
  CHECK_EQ(posY(z), 0.0f);
  CHECK_EQ(z->velocityY, 0.0f);
  // v = g * scale * t after n steps
  CHECK_NEAR(a->velocityY, Rigidbody2DSim::gravity() * 10.0f * kDt, 0.01f);
  const Scalar v = a->velocityY;
  Rigidbody2DSim::setGravity(0.0f);
  CHECK_EQ(Rigidbody2DSim::gravity(), 0.0f);
  const Scalar y = posY(a);
  f.step(10);
  CHECK(posY(a) > y);          // keeps its speed
  CHECK_EQ(a->velocityY, v);   // but gains none
}

TEST(rb2d_dynamic_horizontal_velocity_and_wall) {
  BodyFixture f;
  f.floor();
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Dynamic, 40, 100);
  rb->velocityX = 120.0f;
  f.step(60);
  CHECK_NEAR(posX(rb), 160.0f, 0.5f);            // 120 px/s for one second
  CHECK_EQ(rb->velocityX, 120.0f);               // free movement keeps the speed
  rb->velocityX = 1000.0f;
  f.step(60);                                    // runs into the map border at x = 320
  CHECK_NEAR(posX(rb) + 12.0f, 320.0f, 0.01f);
  CHECK_EQ(rb->velocityX, 0.0f);                 // zeroed on the wall
  rb->velocityX = -1000.0f;
  f.step(60);
  CHECK_NEAR(posX(rb), 0.0f, 0.01f);
  CHECK_EQ(rb->velocityX, 0.0f);
}

TEST(rb2d_dynamic_ceiling_zeroes_upward_speed) {
  BodyFixture f;
  f.floor();
  for (u32 x = 0; x < kW; ++x) f.set(x, 2, kSolid);  // ceiling: underside at y = 48
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Dynamic, 40, 114);  // on the floor
  f.step(2);
  rb->velocityY = -900.0f;  // a jump into the ceiling
  bool hitCeiling = false;
  for (int i = 0; i < 60; ++i) {
    f.step(1);
    if (posY(rb) <= 48.01f && rb->velocityY >= 0.0f && !hitCeiling) { hitCeiling = true; CHECK_NEAR(posY(rb), 48.0f, 0.01f); }
  }
  CHECK(hitCeiling);
  CHECK_NEAR(posY(rb) + 14.0f, 128.0f, 0.01f);  // fell back and rests on the floor again
}

TEST(rb2d_one_way_platform_catches_falling_body) {
  BodyFixture f;
  for (u32 x = 0; x < kW; ++x) f.set(x, 5, kOneWay);  // top at y = 80
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Dynamic, 40, 20);
  f.step(120);
  CHECK_NEAR(posY(rb) + 14.0f, 80.0f, 0.01f);
  CHECK(rb->gameObject()->GetComponent<BoxCollider2D>()->grounded);
}

TEST(rb2d_kinematic_moves_without_gravity) {
  BodyFixture f;
  f.floor();
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Kinematic, 40, 30);
  rb->velocityX = 60.0f;
  f.step(60);
  CHECK_NEAR(posX(rb), 100.0f, 0.5f);
  CHECK_EQ(posY(rb), 30.0f);          // no gravity
  CHECK_EQ(rb->velocityX, 60.0f);     // the velocity is never touched
  rb->velocityX = 0.0f;
  rb->velocityY = 120.0f;
  f.step(120);                        // down onto the floor, collides with tiles but keeps its velocity
  CHECK_NEAR(posY(rb) + 14.0f, 128.0f, 0.01f);
  CHECK_EQ(rb->velocityY, 120.0f);
  rb->velocityY = -60.0f;
  f.step(60);
  CHECK_NEAR(posY(rb) + 14.0f, 68.0f, 0.5f);
}

TEST(rb2d_static_never_moves) {
  BodyFixture f;
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Static, 40, 30);
  rb->velocityX = 100.0f;
  rb->velocityY = 100.0f;
  f.step(60);
  CHECK_EQ(posX(rb), 40.0f);
  CHECK_EQ(posY(rb), 30.0f);
}

TEST(rb2d_skips_disabled_inactive_and_colliderless) {
  BodyFixture f;
  f.bind();
  Rigidbody2D* none = f.body(RigidbodyType::Dynamic, 20, 10, false);   // no BoxCollider2D
  Rigidbody2D* off = f.body(RigidbodyType::Dynamic, 60, 10);
  off->setEnabled(false);
  Rigidbody2D* hidden = f.body(RigidbodyType::Dynamic, 100, 10);
  hidden->gameObject()->SetActive(false);
  Rigidbody2D* live = f.body(RigidbodyType::Dynamic, 140, 10);
  f.step(10);
  CHECK_EQ(posY(none), 10.0f);
  CHECK_EQ(posY(off), 10.0f);
  CHECK_EQ(posY(hidden), 10.0f);
  CHECK(posY(live) > 10.0f);
  CHECK_EQ(none->velocityY, 0.0f);
}

TEST(rb2d_keeps_drop_through_flag_and_script_drop) {
  BodyFixture f;
  for (u32 x = 0; x < kW; ++x) f.set(x, 5, kOneWay);
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Dynamic, 40, 20);
  f.step(120);
  BoxCollider2D* box = rb->gameObject()->GetComponent<BoxCollider2D>();
  CHECK(box->autoDropThrough);  // the flag is restored after every step
  CHECK(box->grounded);
  CHECK(box->DropThrough());    // scripts drop explicitly
  f.step(60);
  CHECK(posY(rb) > 80.0f);
}

TEST(rb2d_is_deterministic) {
  Scalar y[2], vy[2], x[2];
  for (int run = 0; run < 2; ++run) {
    BodyFixture f;
    f.floor();
    f.set(10, 7, kSolid);
    f.bind();
    Rigidbody2D* rb = f.body(RigidbodyType::Dynamic, 30, 0);
    rb->velocityX = 77.0f;
    f.step(200);
    x[run] = posX(rb); y[run] = posY(rb); vy[run] = rb->velocityY;
  }
  CHECK(x[0] == x[1] && y[0] == y[1] && vy[0] == vy[1]);
}

TEST(rb2d_through_gameplay_fixed_step) {
  BodyFixture f;
  f.floor();
  f.bind();
  Rigidbody2D* rb = f.body(RigidbodyType::Dynamic, 40, 20);
  for (int i = 0; i < 120; ++i) Gameplay2D::fixedStep(f.world);  // the real hook runs the bodies first
  CHECK_NEAR(posY(rb) + 14.0f, 128.0f, 0.01f);
}
