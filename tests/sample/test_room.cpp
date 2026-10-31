// F2.05 sample room: slopes, one-way planks, enemies (hurt, stomp, patrol), save point, Camera2D on the real cooked pack.
#include "sample_harness.h"

using namespace sample;

namespace {
GameObject* place(Game& g, const char* name, Scalar x, Scalar y) {
  GameObject* o = GameObject::Find(name);
  if (o) o->transform.setPosition(Vector3(x, y, 0));
  (void)g;
  return o;
}
Scalar foot(BoxCollider2D* b) { return b->bounds().y + b->bounds().height; }
}  // namespace

TEST(room_slope_run_climbs_to_the_plateau_and_back) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("slope"), kRoom));
  g.run(2);
  BoxCollider2D* box = GameObject::Find("Player")->GetComponent<BoxCollider2D>();
  Scalar minFoot = 1000, atPlateau = 0;
  for (int i = 0; i < 160; ++i) {  // walk right in 4 px steps with gravity: over the hill
    box->Move(4.0f, 0);
    box->Move(0, 8.0f);
    const Rect b = box->bounds();
    if (b.x < 430.0f) CHECK(box->grounded);  // (past x = 448 the hill ends in a ledge)
    minFoot = Mathf::Min(minFoot, b.y + b.height);
    const Scalar cx = b.x + b.width * 0.5f;
    if (cx > 350.0f && cx < 400.0f) atPlateau = b.y + b.height;
    if (b.x > 640.0f) break;
  }
  CHECK_NEAR(minFoot, 352.0f, 1.0f);   // plateau height: two tiles above the floor surface
  CHECK_NEAR(atPlateau, 352.0f, 1.0f);
  CHECK_NEAR(foot(box), 416.0f, 0.1f);  // back on the floor on the far side
}

TEST(room_one_way_plank_lands_from_above_and_drops_through) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("plank"), kRoom));
  g.run(2);
  GameObject* player = GameObject::Find("Player");
  BoxCollider2D* box = player->GetComponent<BoxCollider2D>();
  player->transform.setPosition(Vector3(590, 384, 0));  // under plank A (surface y = 320)
  Move2DResult r = box->Move(0, -120.0f);                // rising through the plank is not blocked
  CHECK(!r.hitUp);
  CHECK(foot(box) < 320.0f);
  r = box->Move(0, 200.0f);
  CHECK(r.hitDown);
  CHECK_NEAR(foot(box), 320.0f, 0.1f);
  CHECK(box->DropThrough());
  for (int i = 0; i < 20; ++i) box->Move(0, 20.0f);
  CHECK_NEAR(foot(box), 416.0f, 0.1f);
}

TEST(room_walker_stays_on_its_patrol_and_turns) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("walker"), kRoom));
  GameObject* w = GameObject::Find("Walker1");
  CHECK(w != nullptr);
  Scalar minX = 1e9f, maxX = -1e9f;
  place(g, "Player", 64, 384);
  for (int i = 0; i < 1500; ++i) {  // 25 s: several laps
    g.frame();
    BoxCollider2D* b = GameObject::Find("Walker1")->GetComponent<BoxCollider2D>();
    minX = Mathf::Min(minX, b->bounds().x);
    maxX = Mathf::Max(maxX, b->bounds().x + b->bounds().width);
    CHECK_NEAR(foot(b), 416.0f, 0.1f);
  }
  CHECK(minX >= 479.0f);
  CHECK(maxX <= 737.0f);
  CHECK(minX < 500.0f);   // it reached both ends
  CHECK(maxX > 715.0f);
  CHECK_EQ(GameState::hp, GameState::kMaxHp);
}

TEST(room_walker_turns_at_a_ledge) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("ledge"), kRoom));
  g.run(2);
  place(g, "Player", 64, 384);
  // Put Walker1 on the plateau (surface y = 352, x 320..416). Its patrol limits push it right; at the end of the
  // slope-down tile (x = 448) there is a ledge, and it must turn instead of walking off.
  place(g, "Walker1", 340, 320);
  Scalar lowest = 0;
  for (int i = 0; i < 600; ++i) {
    g.frame();
    BoxCollider2D* b = GameObject::Find("Walker1")->GetComponent<BoxCollider2D>();
    lowest = Mathf::Max(lowest, foot(b));
    CHECK(b->bounds().x + b->bounds().width <= 450.0f);
  }
  CHECK(lowest < 390.0f);  // never dropped to the floor (y = 416)
}

TEST(room_enemy_side_contact_hurts_with_knockback_and_invulnerability) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("hurt"), kRoom));
  g.run(2);
  GameObject* player = place(g, "Player", 545, 384);  // right beside Walker1 (box x 564..588)
  CHECK_EQ(GameState::hp, 3);
  const Scalar x0 = player->transform.position().x;
  g.run(6);
  CHECK_EQ(GameState::hp, 2);
  CHECK(player->transform.position().x < x0);  // knocked away from the enemy (it was to the right)
  CHECK(GameState::messageTicks > 0);
  CHECK(GameState::hurtTicks > 0);
  g.run(10);
  CHECK(GameObject::Find("Walker1") != nullptr);  // not stomped
  CHECK_EQ(GameState::stomps, 0);
  // Invulnerable for a while: standing in it again costs nothing.
  place(g, "Player", 560, 384);
  g.run(20);
  CHECK_EQ(GameState::hp, 2);
}

TEST(room_losing_the_last_hp_respawns_at_the_checkpoint) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("die"), kRoom));
  g.run(2);
  GameState::hp = 1;
  place(g, "Player", 545, 384);
  g.run(4);
  CHECK_EQ(GameState::hp, GameState::kMaxHp);  // refilled
  CHECK_EQ(GameState::lives, GameState::kMaxLives - 1);  // a life was lost
  CHECK_EQ(GameState::exitRequest, 0);
  Vector3 p = GameObject::Find("Player")->transform.position();
  CHECK_NEAR(p.x, 64.0f, 8.0f);
  CHECK_NEAR(p.y, 384.0f, 1.0f);
}

TEST(room_stomping_the_walker_defeats_it_and_bounces) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("stomp"), kRoom));
  g.run(2);
  GameObject* player = place(g, "Player", 556, 330);  // falls onto Walker1
  g.run(24);
  CHECK_EQ(GameState::stomps, 1);
  CHECK_EQ(GameState::hp, GameState::kMaxHp);
  CHECK(GameObject::Find("Walker1") == nullptr);
  CHECK(player->transform.position().y < 384.0f - 10.0f);  // bounced up
  CHECK_EQ(GameState::stomps, 1);
}

TEST(room_hopper_hops_between_its_limits) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("hopper"), kRoom));
  place(g, "Player", 64, 384);
  Scalar minY = 1e9f, minX = 1e9f, maxX = -1e9f;
  for (int i = 0; i < 1200; ++i) {
    g.frame();
    BoxCollider2D* b = GameObject::Find("Hopper1")->GetComponent<BoxCollider2D>();
    minY = Mathf::Min(minY, foot(b));
    minX = Mathf::Min(minX, b->bounds().x);
    maxX = Mathf::Max(maxX, b->bounds().x + b->bounds().width);
  }
  CHECK(minY < 416.0f - 30.0f);  // it left the ground
  CHECK(minX >= 927.0f);
  CHECK(maxX <= 1081.0f);
  CHECK(minX < 980.0f);
  CHECK(maxX > 1030.0f);
}

TEST(room_checkpoint_is_the_respawn_point_and_progress_is_saved_on_entry) {
  fs::path dir = tempDir("savept");
  Game g;
  CHECK(g.boot(cookSample(), dir, kRoom));
  g.run(2);
  // Entering a room saves the progress (room, totals at entry, lives); nothing was saved before.
  CHECK_EQ(Save::LoadInt("room", -1), 1);
  CHECK_EQ(Save::LoadInt("coins", -1), 0);
  CHECK_EQ(Save::LoadInt("ticks", -1), 0);
  CHECK_EQ(Save::LoadInt("lives", -1), GameState::kMaxLives);
  CHECK(fs::exists(dir / "room"));
  CHECK_EQ(GameState::saves, 0);
  place(g, "Player", 876, 384);
  g.run(4);
  CHECK_EQ(GameState::saves, 1);
  CHECK(strstr(GameState::message, "CHECKPOINT") != nullptr);
  CHECK_NEAR(GameState::checkpointX, 880.0f, 0.1f);
  // Out of HP at another place: the life is lost and the player is back at the flag with full HP.
  GameState::hp = 1;
  place(g, "Player", GameObject::Find("Walker1")->GetComponent<BoxCollider2D>()->bounds().x - 22.0f, 384);   // beside the walker
  g.run(4);
  CHECK_EQ(GameState::lives, GameState::kMaxLives - 1);
  CHECK_NEAR(GameObject::Find("Player")->transform.position().x, 880.0f, 8.0f);
  CHECK_EQ(GameState::hp, GameState::kMaxHp);
}

TEST(room_camera_scrolls_with_the_player_inside_the_room) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("camera"), kRoom));
  g.run(3);
  Camera2D* cam = Camera2D::main();
  CHECK(cam != nullptr);
  if (!cam) return;
  CHECK_NEAR(cam->viewX, 0.0f, 0.5f);  // start of the room: clamped at the left
  place(g, "Player", 1000, 384);
  g.run(10);
  CHECK(cam->viewX > 600.0f);
  CHECK(cam->viewX + cam->effectiveViewWidth() <= 1280.5f);
  place(g, "Player", 1200, 384);
  g.run(10);
  CHECK_NEAR(cam->viewX + cam->effectiveViewWidth(), 1280.0f, 0.5f);  // clamped at the right wall
}
