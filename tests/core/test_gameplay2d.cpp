// F2.05: slopes, one-way tiles, layer matrix, Camera2D and DontDestroyOnLoad on a hand-built tilemap (no pack file).
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

constexpr u32 kW = 20, kH = 10, kTs = 16;
enum : u16 { kEmpty = 0xFFFF, kBlank = 0, kSolid = 1, kOneWay = 2, kS45R = 3, kS45L = 4, kS22Rlo = 5, kS22Rhi = 6 };

struct MapFixture {
  Arena arena;
  World world;
  u16 tiles[kW * kH];
  u8 flags[8];
  alignas(16) u8 blob[kW * kH * 2 + 16];
  PackTilemapRec rec = {};
  PackAssets assets;
  GameObject* mapObj = nullptr;

  MapFixture() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 32;
    c.maxComponentsPerType = 16;
    world.init(c, arena);
    for (u16& t : tiles) t = kEmpty;
    memset(flags, 0, sizeof flags);
    flags[kSolid] = PackTileFlag::kSolid;
    flags[kOneWay] = PackTileFlag::kOneWay;
    flags[kS45R] = kSlope45R << PackTileFlag::kSlopeShift;
    flags[kS45L] = kSlope45L << PackTileFlag::kSlopeShift;
    flags[kS22Rlo] = kSlope22R_lo << PackTileFlag::kSlopeShift;
    flags[kS22Rhi] = kSlope22R_hi << PackTileFlag::kSlopeShift;
  }
  ~MapFixture() { GfxAssets::unbind(); }
  void set(u32 x, u32 y, u16 t) { tiles[y * kW + x] = t; }
  // Floor on rows 8 and 9.
  void floor() {
    for (u32 x = 0; x < kW; ++x) { set(x, 8, kSolid); set(x, 9, kSolid); }
  }
  void bind() {
    memcpy(blob, tiles, sizeof tiles);
    memcpy(blob + sizeof tiles, flags, sizeof flags);
    Guid::parse("00000000-0000-4000-8000-0000000000a1", rec.guid);
    rec.width = kW; rec.height = kH; rec.tileSize = kTs;
    rec.tilesOffset = 0;
    rec.solidOffset = sizeof tiles;
    rec.solidCount = 8;
    assets.blob = blob;
    assets.blobSize = sizeof blob;
    assets.tilemaps = &rec;
    assets.tilemapCount = 1;
    GfxAssets::bind(nullptr, &assets, nullptr, 0);
    mapObj = world.createObject("map");
    Tilemap* tm = mapObj->AddComponent<Tilemap>();
    tm->map = rec.guid;
    mapObj->AddComponent<TilemapCollider>();
  }
  BoxCollider2D* box(Scalar x, Scalar y, Scalar w = 12, Scalar h = 14) {
    GameObject* go = world.createObject("box");
    go->transform.setPosition(Vector3(x, y, 0));
    BoxCollider2D* b = go->AddComponent<BoxCollider2D>();
    b->sizeX = w;
    b->sizeY = h;
    return b;
  }
};

Scalar bottomOf(const BoxCollider2D* b) { return b->bounds().y + b->bounds().height; }
Scalar footX(const BoxCollider2D* b) { return b->bounds().x + b->bounds().width * 0.5f; }
Scalar absf(Scalar v) { return v < 0 ? -v : v; }

}  // namespace

TEST(g2d_flat_floor_unchanged) {
  MapFixture f;
  f.floor();
  f.bind();
  BoxCollider2D* b = f.box(40, 50);
  Move2DResult r;
  for (int i = 0; i < 40 && !r.hitDown; ++i) r = b->Move(0, 4);
  CHECK(r.hitDown);
  CHECK_NEAR(bottomOf(b), 128.0f, 0.01f);
  CHECK(b->grounded);
  r = b->Move(400, 0);  // into the right wall at x = 320
  CHECK(r.hitRight);
  CHECK_NEAR(b->bounds().x + b->bounds().width, 320.0f, 0.01f);
}

TEST(g2d_slope_walk_up_and_down) {
  MapFixture f;
  f.floor();
  // 45 degree slope "/" on row 7, cols 4..6 (rises 48 px), then a plateau of solid blocks on rows 5..7 from col 7.
  f.set(4, 7, kS45R);
  f.set(5, 6, kS45R); f.set(5, 7, kSolid);
  f.set(6, 5, kS45R); f.set(6, 6, kSolid); f.set(6, 7, kSolid);
  for (u32 x = 7; x < 12; ++x) for (u32 y = 5; y < 8; ++y) f.set(x, y, kSolid);
  f.bind();
  // Surface y at foot x: 128 at x=64 ... 80 at x=112; plateau top 80.
  BoxCollider2D* b = f.box(20, 114);
  for (int i = 0; i < 4; ++i) b->Move(0, 2);
  CHECK(b->grounded);
  CHECK_NEAR(bottomOf(b), 128.0f, 0.01f);
  Scalar last = bottomOf(b);
  Scalar worstJump = 0;
  for (int i = 0; i < 200 && footX(b) < 130; ++i) {
    b->Move(1, 1);
    CHECK(b->grounded);
    const Scalar d = absf(bottomOf(b) - last);
    if (d > worstJump) worstJump = d;
    if (d > 1.01f) fprintf(stderr, "DBG up jump %f at foot %f bottom %f\n", d, footX(b), bottomOf(b));
    last = bottomOf(b);
    // The leading (right) corner carries the box up the slope.
    if (footX(b) + 6 > 64 && footX(b) + 6 < 112) CHECK_NEAR(bottomOf(b), 128.0f - (footX(b) + 6.0f - 64.0f), 1.01f);
  }
  CHECK_NEAR(bottomOf(b), 80.0f, 0.01f);   // standing on the plateau
  CHECK(worstJump <= 1.01f);               // no jitter: a 1 px step on a 45 degree slope moves at most 1 px
  // And back down.
  worstJump = 0;
  for (int i = 0; i < 300 && footX(b) > 30; ++i) {
    b->Move(-1, 1);
    CHECK(b->grounded);
    const Scalar d = absf(bottomOf(b) - last);
    if (d > worstJump) worstJump = d;
    if (d > 1.01f) fprintf(stderr, "DBG down jump %f at foot %f bottom %f\n", d, footX(b), bottomOf(b));
    last = bottomOf(b);
  }
  CHECK_NEAR(bottomOf(b), 128.0f, 0.01f);
  CHECK(worstJump <= 1.01f);
}

TEST(g2d_slope_22_degrees) {
  MapFixture f;
  f.floor();
  f.set(4, 7, kS22Rlo);  // 128 -> 120 over x 64..80
  f.set(5, 7, kS22Rhi);  // 120 -> 112 over x 80..96
  for (u32 x = 6; x < 10; ++x) f.set(x, 7, kSolid);
  f.bind();
  BoxCollider2D* b = f.box(30, 114);
  for (int i = 0; i < 4; ++i) b->Move(0, 2);
  for (int i = 0; i < 100 && footX(b) < 120; ++i) { b->Move(1, 1); CHECK(b->grounded); }
  CHECK_NEAR(bottomOf(b), 112.0f, 0.01f);
  for (int i = 0; i < 100 && footX(b) > 70; ++i) b->Move(-1, 1);
  CHECK(b->grounded);
  CHECK_NEAR(bottomOf(b), 128.0f - (footX(b) + 6.0f - 64.0f) * 0.5f, 1.01f);
}

TEST(g2d_slope_left_facing) {
  MapFixture f;
  f.floor();
  f.set(10, 7, kS45L);  // "\" is high on the left: 112 at x=160 down to 128 at x=176
  f.set(9, 7, kSolid);
  f.bind();
  BoxCollider2D* b = f.box(100, 114);  // on the floor, left of the solid block at col 9 (x 144)
  for (int i = 0; i < 4; ++i) b->Move(0, 2);
  CHECK_NEAR(bottomOf(b), 128.0f, 0.01f);
  // Start on top of the block and walk off to the right, down the "\" slope.
  b = f.box(146, 98);
  for (int i = 0; i < 6; ++i) b->Move(0, 2);
  CHECK_NEAR(bottomOf(b), 112.0f, 0.01f);
  for (int i = 0; i < 100 && footX(b) < 172; ++i) { b->Move(1, 1); CHECK(b->grounded); }
  CHECK_NEAR(bottomOf(b), 112.0f + (footX(b) - 6.0f - 160.0f), 1.01f);
}

TEST(g2d_one_way_platform) {
  MapFixture f;
  f.floor();
  for (u32 x = 5; x < 10; ++x) f.set(x, 5, kOneWay);  // top at y = 80
  f.bind();
  // From above: lands.
  BoxCollider2D* b = f.box(100, 20);
  Move2DResult r;
  for (int i = 0; i < 40 && !r.hitDown; ++i) r = b->Move(0, 3);
  CHECK(r.hitDown);
  CHECK_NEAR(bottomOf(b), 80.0f, 0.01f);
  CHECK(b->grounded);
  // Sideways into the platform's cell from the side does not block (box is above it, no overlap anyway).
  // From below: passes through, then falls back and lands on top.
  BoxCollider2D* c = f.box(100, 110);
  for (int i = 0; i < 12; ++i) c->Move(0, -4);
  CHECK(bottomOf(c) < 80.0f);
  for (int i = 0; i < 40; ++i) c->Move(0, 3);
  CHECK_NEAR(bottomOf(c), 80.0f, 0.01f);
  // Drop through: stand on it, ignore it, fall to the floor.
  CHECK(b->DropThrough());
  for (int i = 0; i < 60; ++i) b->Move(0, 2);
  CHECK_NEAR(bottomOf(b), 128.0f, 0.01f);
  // The platform works again afterwards: jump up through it, fall back and land on top, then it can be dropped again.
  b->Move(0, -60);
  for (int i = 0; i < 80; ++i) b->Move(0, 2);
  CHECK_NEAR(bottomOf(b), 80.0f, 0.01f);
  CHECK(b->DropThrough());
  for (int i = 0; i < 60; ++i) b->Move(0, 2);
  CHECK_NEAR(bottomOf(b), 128.0f, 0.01f);
  CHECK(!b->DropThrough());  // on the floor: nothing to drop through
}

TEST(g2d_one_way_drop_with_down_and_south) {
  MapFixture f;
  f.floor();
  for (u32 x = 5; x < 10; ++x) f.set(x, 5, kOneWay);
  f.bind();
  BoxCollider2D* b = f.box(100, 20);
  for (int i = 0; i < 40; ++i) b->Move(0, 3);
  CHECK_NEAR(bottomOf(b), 80.0f, 0.01f);
  VirtualGamepad pad;
  pad.held.down = true;
  pad.held.south = true;
  Input::update(pad);
  b->Move(0, 1);
  CHECK(b->dropping);
  for (int i = 0; i < 60; ++i) b->Move(0, 2);
  CHECK_NEAR(bottomOf(b), 128.0f, 0.01f);
  VirtualGamepad idle;
  Input::update(idle);
}

TEST(g2d_layer_matrix_filters_pairs) {
  MapFixture f;
  f.floor();
  f.bind();
  f.mapObj->layer = 3;
  BoxCollider2D* b = f.box(40, 50);
  b->gameObject()->layer = 5;
  CHECK(Physics2D::layersCollide(b->gameObject(), f.mapObj));
  Physics2D::IgnoreLayerCollision(3, 5, true);
  CHECK(Physics2D::GetIgnoreLayerCollision(5, 3));
  CHECK(!Physics2D::layersCollide(b->gameObject(), f.mapObj));
  CHECK(Physics2D::layersCollide(b->gameObject(), b->gameObject()));  // 5 vs 5 untouched
  Move2DResult r = b->Move(0, 100);  // falls straight through the floor
  CHECK(!r.hitDown);
  CHECK(b->bounds().y > 128.0f);
  // Another layer still collides.
  BoxCollider2D* c = f.box(40, 50);
  c->gameObject()->layer = 6;
  r = c->Move(0, 100);
  CHECK(r.hitDown);
  Physics2D::IgnoreLayerCollision(3, 5, false);
  CHECK(!Physics2D::GetIgnoreLayerCollision(3, 5));
  Physics2D::resetMatrix();
}

namespace {
int g_enters = 0;
struct EnterCounter : Behaviour {
  REFLECT(EnterCounter)
  void OnTriggerEnter(GameObject*) override { ++g_enters; }
};
}  // namespace

TEST(g2d_layer_matrix_filters_triggers) {
  MapFixture f;
  f.bind();
  GameObject* trig = f.world.createObject("trigger");
  trig->transform.setPosition(Vector3(100, 100, 0));
  trig->layer = 2;
  BoxCollider2D* tb = trig->AddComponent<BoxCollider2D>();
  tb->isTrigger = true;
  trig->AddComponent<EnterCounter>();
  BoxCollider2D* a = f.box(100, 100);
  a->gameObject()->layer = 4;
  Physics2D::IgnoreLayerCollision(2, 4, true);
  g_enters = 0;
  f.world.startPending();
  Gameplay2D::resetTriggerMemory();
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_enters, 0);
  Physics2D::IgnoreLayerCollision(2, 4, false);
  Gameplay2D::fixedStep(f.world);
  CHECK_EQ(g_enters, 1);
  Physics2D::resetMatrix();
}

TEST(g2d_camera_follow_dead_zone_and_bounds) {
  MapFixture f;
  f.bind();
  GameObject* tgt = f.world.createObject("target");
  Guid::parse("00000000-0000-4000-8000-0000000000b2", tgt->guid);
  GameObject* camObj = f.world.createObject("cam");
  Camera2D* cam = camObj->AddComponent<Camera2D>();
  cam->target = tgt->guid;
  cam->viewWidth = 160;
  cam->viewHeight = 120;
  cam->deadZoneW = 40;
  cam->deadZoneH = 20;
  cam->useBounds = true;
  cam->boundsX = 0; cam->boundsY = 0; cam->boundsW = 320; cam->boundsH = 160;
  // The tilemap object gives the bounds: 20 x 10 tiles of 16 px = 320 x 160.
  Guid::parse("00000000-0000-4000-8000-0000000000c3", f.mapObj->guid);
  cam->boundsTilemap = f.mapObj->guid;
  tgt->transform.setPosition(Vector3(80, 60, 0));  // view centre: inside the dead zone
  cam->UpdateView();
  CHECK_NEAR(cam->viewX, 0.0f, 0.01f);
  CHECK_NEAR(cam->viewY, 0.0f, 0.01f);
  tgt->transform.setPosition(Vector3(110, 60, 0));  // dead zone spans x 60..100, so the view moves by 10
  cam->UpdateView();
  CHECK_NEAR(cam->viewX, 10.0f, 0.01f);
  tgt->transform.setPosition(Vector3(105, 60, 0));  // back inside: no move
  cam->UpdateView();
  CHECK_NEAR(cam->viewX, 10.0f, 0.01f);
  tgt->transform.setPosition(Vector3(1000, 900, 0));  // far away: clamped at the right and bottom edge
  cam->UpdateView();
  CHECK_NEAR(cam->viewX, 160.0f, 0.01f);
  CHECK_NEAR(cam->viewY, 40.0f, 0.01f);
  tgt->transform.setPosition(Vector3(-500, -500, 0));
  cam->UpdateView();
  CHECK_NEAR(cam->viewX, 0.0f, 0.01f);
  CHECK_NEAR(cam->viewY, 0.0f, 0.01f);
  CHECK(Camera2D::main() == cam);
  // Pixel snapping rounds the view to whole pixels.
  tgt->transform.setPosition(Vector3(110.4f, 60, 0));
  cam->UpdateView();
  CHECK_NEAR(cam->viewX, 10.0f, 0.001f);
  cam->pixelSnap = false;
  tgt->transform.setPosition(Vector3(120.4f, 60, 0));
  cam->UpdateView();
  CHECK(absf(cam->viewX - Mathf::Floor(cam->viewX)) > 0.1f);
}

TEST(g2d_camera_smaller_room_is_centered) {
  MapFixture f;
  f.bind();
  GameObject* camObj = f.world.createObject("cam");
  Camera2D* cam = camObj->AddComponent<Camera2D>();
  cam->viewWidth = 200;
  cam->viewHeight = 100;
  cam->useBounds = true;
  cam->boundsW = 100; cam->boundsH = 100;
  cam->UpdateView();
  CHECK_NEAR(cam->viewX, -50.0f, 0.01f);
  CHECK_NEAR(cam->viewY, 0.0f, 0.01f);
}

TEST(g2d_dont_destroy_on_load_survives_destroy_all) {
  Arena arena;
  arena.create(1 << 18);
  World world;
  WorldConfig c;
  c.maxObjects = 16;
  c.maxComponentsPerType = 8;
  world.init(c, arena);
  GameObject* parent = world.createObject("parent");
  GameObject* child = world.createObject("child", parent);
  child->transform.setPosition(Vector3(5, 6, 0));
  world.createObject("other");
  GameObject* keep = world.createObject("keep");
  keep->transform.setPosition(Vector3(1, 2, 3));
  DontDestroyOnLoad(keep);
  DontDestroyOnLoad(child);
  CHECK(child->transform.parent() == nullptr);          // moved to the scene root
  CHECK_NEAR(child->transform.position().x, 5.0f, 0.001f);
  world.destroyAllImmediate(true);
  CHECK_EQ(world.objectCount(), 2u);
  CHECK(world.findByName("keep") != nullptr);
  CHECK(world.findByName("child") != nullptr);
  CHECK(world.findByName("parent") == nullptr);
  CHECK(world.findByName("other") == nullptr);
  world.destroyAllImmediate(false);                      // a full clear still removes everything
  CHECK_EQ(world.objectCount(), 0u);
}
