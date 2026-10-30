// F2.05 unit tests on the cooked platformer sample: tilemap collision, animation, scenes, UI, saves, multi-scene packs.
#include <unistd.h>

#include "sample_harness.h"

using namespace sample;

TEST(gameplay_pack_has_two_scenes_and_assets) {
  fs::path pack = cookSample();
  CHECK(!pack.empty());
  Game g;
  CHECK(g.boot(pack, tempDir("scenes")));
  CHECK_EQ(SceneManager::sceneCount(), 3u);
  CHECK_EQ(strcmp(SceneManager::sceneName(0), "Title"), 0);
  CHECK_EQ(strcmp(SceneManager::sceneName(1), "Room"), 0);
  CHECK_EQ(strcmp(SceneManager::sceneName(2), "RoomBot"), 0);
  CHECK_EQ(SceneManager::sceneIndexByName("Room"), 1);
  CHECK_EQ(g.loaded.assets.tilemapCount, 1u);
  CHECK_EQ(g.loaded.assets.animCount, 9u);
  // Scene 0 only: the title objects, no player.
  CHECK(GameObject::Find("StartButton") != nullptr);
  CHECK(GameObject::Find("Player") == nullptr);
}

TEST(gameplay_scene_load_is_deferred_to_end_of_frame) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("defer")));
  g.run(2);
  CHECK(SceneManager::LoadScene("Room"));
  CHECK(!SceneManager::LoadScene("Nope"));
  CHECK(SceneManager::loadPending());
  CHECK(GameObject::Find("Player") == nullptr);  // not yet: nothing changes before the frame ends
  g.frame();
  CHECK(!SceneManager::loadPending());
  CHECK_EQ(SceneManager::activeSceneIndex(), 1);
  CHECK(GameObject::Find("Player") != nullptr);
  CHECK(GameObject::Find("StartButton") == nullptr);
  CHECK(SceneManager::LoadScene(0));
  g.run(2);
  CHECK(GameObject::Find("Player") == nullptr);
  CHECK(GameObject::Find("StartButton") != nullptr);
  // A scene can be loaded over and over without running out of pool slots.
  for (int i = 0; i < 20; ++i) { SceneManager::LoadScene(1); g.frame(); SceneManager::LoadScene(0); g.frame(); }
  CHECK(GameObject::Find("StartButton") != nullptr);
}

TEST(gameplay_ui_button_press_loads_room) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("ui")));
  g.run(3);
  CHECK(GameObject::Find("Player") == nullptr);
  g.pad.held.south = true;  // press edge on this frame
  g.frame();
  g.pad.held.south = false;
  g.frame();
  CHECK_EQ(SceneManager::activeSceneIndex(), 1);
  CHECK(GameObject::Find("Player") != nullptr);
}

TEST(gameplay_tilemap_collision_and_ground_check) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("tiles"), 1));
  g.run(3);
  GameObject* player = GameObject::Find("Player");
  CHECK(player != nullptr);
  if (!player) return;
  BoxCollider2D* box = player->GetComponent<BoxCollider2D>();
  CHECK(box != nullptr);
  CHECK(box->grounded);
  // Box bottom rests exactly on the floor tile row (y = 13 * 32 = 416).
  Rect b = box->bounds();
  CHECK_NEAR(b.y + b.height, 416.0f, 0.05f);
  // Walking left stops at the wall tile column 0 (x = 32).
  Move2DResult r = box->Move(-1000.0f, 0);
  CHECK(r.hitLeft);
  CHECK_NEAR(box->bounds().x, 32.0f, 0.05f);
  // Walking right climbs the hill, crosses the floor and stops at the first stair block (column 34, x = 1088).
  for (int i = 0; i < 300 && !r.hitRight; ++i) { r = box->Move(8.0f, 0); box->Move(0, 16.0f); }
  CHECK(r.hitRight);
  CHECK_NEAR(box->bounds().x + box->bounds().width, 1088.0f, 0.05f);
  r = box->Move(0, 500.0f);
  // Falling a long way never tunnels through the floor.
  r = box->Move(0, 500.0f);
  CHECK(r.hitDown);
  CHECK_NEAR(box->bounds().y + box->bounds().height, 416.0f, 0.05f);
  // Rising hits the platform/ceiling above.
  r = box->Move(0, -2000.0f);
  CHECK(r.hitUp);
  CHECK(box->bounds().y >= 32.0f - 0.05f);
}

TEST(gameplay_sprite_animator_plays_clips_by_name) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("anim"), 1));
  g.run(2);
  GameObject* player = GameObject::Find("Player");
  CHECK(player != nullptr);
  if (!player) return;
  SpriteAnimator* an = player->GetComponent<SpriteAnimator>();
  SpriteRenderer* sr = player->GetComponent<SpriteRenderer>();
  CHECK(an && sr);
  if (!an || !sr) return;
  CHECK_EQ(strcmp(an->currentClipName(), "player_idle"), 0);
  CHECK(an->Play("player_run"));
  CHECK(!an->Play("does_not_exist"));
  CHECK_EQ(sr->curY, 32);  // run frames are on the second row of the sheet
  CHECK_EQ(strcmp(an->currentClipName(), "player_run"), 0);
  // The coin loops coin_spin at 8 fps over 16 px frames; the player script switches clips on its own.
  GameObject* coin = GameObject::Find("Coin1");
  CHECK(coin != nullptr);
  if (!coin) return;
  SpriteAnimator* ca = coin->GetComponent<SpriteAnimator>();
  SpriteRenderer* cs = coin->GetComponent<SpriteRenderer>();
  CHECK(ca && cs);
  if (!ca || !cs) return;
  CHECK_EQ(strcmp(ca->currentClipName(), "coin_spin"), 0);
  const i32 first = cs->curX;
  g.run(10);  // 1/6 s = 1 frame later
  CHECK(cs->curX != first);
  CHECK(ca->isPlaying());
  g.run(60);  // loops: still inside the 4-frame strip
  CHECK(cs->curX >= 0 && cs->curX <= 48);
  // Same clip again does not restart it.
  const u32 f = ca->currentFrame();
  ca->Play("coin_spin");
  CHECK_EQ(ca->currentFrame(), f);
}

TEST(gameplay_coin_trigger_collects_once) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("coin"), 1));
  g.run(2);
  GameObject* player = GameObject::Find("Player");
  GameObject* coin = GameObject::Find("Coin1");  // on the floor at (168, 392)
  CHECK(player && coin);
  if (!player || !coin) return;
  player->transform.setPosition(Vector3(160, 384, 0));
  CHECK_EQ(GameState::coins, 0);
  g.run(3);
  CHECK_EQ(GameState::coins, 1);
  CHECK(GameObject::Find("Coin1") == nullptr);
  g.run(5);
  CHECK_EQ(GameState::coins, 1);
}

TEST(gameplay_save_store_round_trip_and_corruption) {
  fs::path dir = tempDir("save");
  StdioFileSystem sfs("", nullptr, (dir.string() + "/").c_str());
  FileSystemSaveStore store(sfs);
  const u8 data[5] = {1, 2, 3, 4, 5};
  CHECK(!store.Exists("slot1"));
  CHECK(store.SaveFile("slot1", data, 5));
  CHECK(store.Exists("slot1"));
  u8 back[16] = {};
  CHECK_EQ(store.LoadFile("slot1", back, 16), 5);
  CHECK_EQ(memcmp(back, data, 5), 0);
  CHECK_EQ(store.LoadFile("slot1", back, 3), 3);  // truncated to the caller's buffer
  CHECK_EQ(store.LoadFile("missing", back, 16), -1);
  CHECK(!store.SaveFile("../evil", data, 5));
  CHECK(!store.SaveFile("a/b", data, 5));
  // A flipped payload byte is detected by the checksum.
  FILE* f = fopen((dir / "slot1").string().c_str(), "r+b");
  CHECK(f != nullptr);
  if (f) { fseek(f, 12, SEEK_SET); fputc(99, f); fclose(f); }
  CHECK_EQ(store.LoadFile("slot1", back, 16), -1);
  // Save:: helpers
  Save::use(&store);
  CHECK(Save::SaveInt("best", 1234));
  CHECK_EQ(Save::LoadInt("best", 0), 1234);
  CHECK_EQ(Save::LoadInt("none", 7), 7);
  Save::use(nullptr);
  CHECK(!Save::SaveInt("best", 1));
}
