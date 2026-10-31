// F4.01 game flow on the cooked sample: lives and game over, checkpoints, pause, room exits, the goal and the results, Title with
// New Game / Continue, saves (progress and best time). Everything runs on the real GameLoop headless and in game time.
#include "sample_harness.h"

using namespace sample;

namespace {
GameObject* place(const char* name, Scalar x, Scalar y) {
  GameObject* o = GameObject::Find(name);
  if (o) o->transform.setPosition(Vector3(x, y, 0));
  return o;
}
// Puts the player so its box overlaps the left side of Walker1 (a side hit).
void besideWalker() {
  GameObject* w = GameObject::Find("Walker1");
  if (w) place("Player", w->GetComponent<BoxCollider2D>()->bounds().x - 22.0f, 384);
}
void press(Game& g, bool VirtualGamepad::Buttons::*button) {
  g.pad.held.*button = true;
  g.frame();
  g.pad.held.*button = false;
  g.frame();
}
bool focused(const char* buttonName) {
  GameObject* o = GameObject::Find(buttonName);
  UIButton* b = o ? o->GetComponent<UIButton>() : nullptr;
  return b && b->isFocused();
}
}  // namespace

TEST(flow_every_room_has_its_coins_exit_and_checkpoint) {
  static const u32 rooms[3] = {kRoom, kRoom2, kRoom3};
  for (int r = 0; r < 3; ++r) {
    Game g;
    CHECK(g.boot(cookSample(), tempDir("rooms"), rooms[r]));
    g.run(2);
    GameObject* coins[32];
    CHECK_EQ(static_cast<i32>(GameObject::FindGameObjectsWithTag("Coin", coins, 32)), GameState::kRoomCoins[r]);
    CHECK(GameObject::Find("Player") != nullptr);
    CHECK(GameObject::Find("SavePoint") != nullptr);
    CHECK(GameObject::Find(r == 2 ? "Goal" : "Exit") != nullptr);
    CHECK(GameObject::Find(r == 2 ? "Exit" : "Goal") == nullptr);
    CHECK_EQ(GameState::room, r + 1);
  }
  CHECK_EQ(GameState::kRoomCoins[0] + GameState::kRoomCoins[1] + GameState::kRoomCoins[2], GameState::kTotalCoins);
  // Room 2 has the slope hills, the plank staircase and two hoppers; room 3 is the gauntlet of five enemies.
  Game g2;
  CHECK(g2.boot(cookSample(), tempDir("rooms2"), kRoom2));
  CHECK(GameObject::Find("Hopper1") && GameObject::Find("Hopper2"));
  GameObject* e[16];
  CHECK_EQ(GameObject::FindGameObjectsWithTag("Enemy", e, 16), 2u);
  Game g3;
  CHECK(g3.boot(cookSample(), tempDir("rooms3"), kRoom3));
  CHECK_EQ(GameObject::FindGameObjectsWithTag("Enemy", e, 16), 5u);
}

TEST(flow_losing_all_lives_ends_in_game_over_and_continue_retries_the_room) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("over"), kRoom));
  g.run(2);
  CHECK_EQ(GameState::lives, 3);
  for (int life = 3; life >= 2; --life) {   // two lives are lost and the player restarts at the checkpoint
    GameState::hp = 1;
    besideWalker();
    g.run(6);
    CHECK_EQ(GameState::lives, life - 1);
    CHECK_EQ(GameState::exitRequest, 0);
    CHECK_EQ(GameState::hp, GameState::kMaxHp);
    g.run(100);   // the invulnerability blink ends
  }
  GameState::coins = 4;   // coins of this run (the room entry snapshot said 0)
  GameState::hp = 1;
  besideWalker();
  g.run(6);
  CHECK_EQ(GameState::lives, 0);
  CHECK_EQ(GameState::exitRequest, 3);
  CHECK(GameState::Frozen());
  g.run(60);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom));   // the HUD waits a moment before the game over scene
  g.run(80);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kGameOver));
  g.run(3);
  UIText* roomText = GameObject::Find("OverRoom")->GetComponent<UIText>();
  CHECK(strstr(roomText->text, "ROOM 1") != nullptr);
  UIText* coinsText = GameObject::Find("OverCoins")->GetComponent<UIText>();
  CHECK(strstr(coinsText->text, "COINS 4") != nullptr);
  CHECK(focused("RetryButton"));
  press(g, &VirtualGamepad::Buttons::south);   // Continue
  g.run(2);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom));
  CHECK_EQ(GameState::lives, GameState::kMaxLives);   // full lives again
  CHECK_EQ(GameState::coins, 0);                      // back to the totals when the room was entered
  CHECK_EQ(GameState::exitRequest, 0);
  CHECK(GameObject::Find("Coin1") != nullptr);
}

TEST(flow_game_over_quit_goes_to_the_title) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("overquit"), kGameOver));
  g.run(3);
  CHECK(focused("RetryButton"));
  g.pad.held.down = true;
  g.frame();
  g.pad.held.down = false;
  g.frame();
  CHECK(focused("QuitButton"));
  press(g, &VirtualGamepad::Buttons::south);
  g.run(2);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kTitle));
}

TEST(flow_start_pauses_the_game_time_and_the_menu_resumes_or_quits) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("pause"), kRoom));
  g.run(10);
  CHECK(!GameState::paused);
  CHECK(!focused("ResumeButton"));
  const i32 t0 = GameState::ticks;
  CHECK(t0 > 0);
  GameObject* player = GameObject::Find("Player");
  const Vector3 p0 = player->transform.position();
  g.pad.held.start = true;
  g.frame();
  g.pad.held.start = false;
  g.run(2);
  CHECK(GameState::paused);
  CHECK_NEAR(Time::timeScale(), 0.0f, 1e-6);
  CHECK(focused("ResumeButton"));          // the pause menu shows and takes the focus
  const i32 tp = GameState::ticks;
  g.pad.leftX = 1.0f;                      // the stick does not move the player while paused
  g.run(40);
  g.pad.leftX = 0.0f;
  CHECK_EQ(GameState::ticks, tp);          // game time stands still
  CHECK_NEAR(player->transform.position().x, p0.x, 0.01f);
  press(g, &VirtualGamepad::Buttons::south);   // Resume (the South press must not make the player jump)
  CHECK(!GameState::paused);
  CHECK_NEAR(Time::timeScale(), 1.0f, 1e-6);
  g.run(30);
  CHECK(GameState::ticks >= tp + 25);
  CHECK_NEAR(player->transform.position().y, p0.y, 0.5f);
  CHECK(!focused("ResumeButton"));
  // Start toggles: pause, Start again resumes.
  press(g, &VirtualGamepad::Buttons::start);
  CHECK(GameState::paused);
  press(g, &VirtualGamepad::Buttons::start);
  CHECK(!GameState::paused);
  // Pause, Down, South: Quit to title.
  press(g, &VirtualGamepad::Buttons::start);
  g.run(2);
  press(g, &VirtualGamepad::Buttons::down);
  CHECK(focused("QuitButton"));
  press(g, &VirtualGamepad::Buttons::south);
  g.run(2);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kTitle));
  CHECK_NEAR(Time::timeScale(), 1.0f, 1e-6);   // the title is not frozen
  CHECK(!GameState::paused);
}

TEST(flow_room_exit_loads_the_next_room_keeps_coins_and_saves_the_progress) {
  Game g;
  fs::path dir = tempDir("exit");
  LogCapture log;
  log::addSink(&log);
  CHECK(g.boot(cookSample(), dir, kRoom));
  g.run(2);
  GameState::coins = 3;
  GameState::lives = 2;
  place("Player", 1216, 288);
  g.run(3);
  CHECK_EQ(GameState::exitRequest, 1);
  CHECK(log.has("rensga: room cleared 1 coins=3"));
  const i32 ticksAtExit = GameState::ticks;
  g.run(30);
  CHECK_EQ(GameState::ticks, ticksAtExit);    // game time stops in the transition
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom));
  g.run(60);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom2));
  g.run(2);
  CHECK_EQ(GameState::room, 2);
  CHECK_EQ(GameState::coins, 3);
  CHECK_EQ(GameState::lives, 2);
  CHECK_EQ(GameState::exitRequest, 0);
  CHECK(GameState::ticks >= ticksAtExit);
  // Entering the room saved the progress: Continue resumes here.
  CHECK_EQ(Save::LoadInt("room", -1), 2);
  CHECK_EQ(Save::LoadInt("coins", -1), 3);
  CHECK_EQ(Save::LoadInt("lives", -1), 2);
  CHECK_EQ(Save::LoadInt("ticks", -1), ticksAtExit);
  log::removeSink(&log);
}

TEST(flow_goal_ends_the_game_and_the_results_show_time_coins_and_the_best_time) {
  fs::path dir = tempDir("goal");
  Game g;
  LogCapture log;
  log::addSink(&log);
  CHECK(g.boot(cookSample(), dir, kRoom3));
  g.run(2);
  CHECK_EQ(Save::LoadInt("best_time", -1), -1);   // an empty save
  GameState::coins = 17;
  GameState::ticks = 7200;
  place("Player", 1536, 288);
  g.run(3);
  CHECK_EQ(GameState::exitRequest, 2);
  CHECK(log.has("rensga: game complete coins=17 time=72"));
  CHECK(log.has("seconds="));
  CHECK_EQ(Save::LoadInt("best_time", -1), GameState::finalTicks);
  CHECK_EQ(Save::LoadInt("room", -1), 0);   // finished: nothing to continue
  CHECK(GameState::newRecord);
  g.run(120);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kResults));
  g.run(3);
  CHECK(strstr(GameObject::Find("ResTime")->GetComponent<UIText>()->text, "TIME 2:0") != nullptr);   // 7200 ticks at 60 Hz = 2:00
  CHECK(strstr(GameObject::Find("ResCoins")->GetComponent<UIText>()->text, "COINS 17/19") != nullptr);
  CHECK(strstr(GameObject::Find("ResBest")->GetComponent<UIText>()->text, "NEW BEST TIME") != nullptr);
  const i32 best = GameState::finalTicks;
  // A slower run does not replace the best time; a faster one does.
  GameState::exitRequest = 0;
  GameState::ticks = best + 600;
  GameState::ReachGoal();
  CHECK(!GameState::newRecord);
  CHECK_EQ(Save::LoadInt("best_time", -1), best);
  GameState::exitRequest = 0;
  GameState::ticks = best - 300;
  GameState::coins = 19;
  GameState::ReachGoal();
  CHECK(GameState::newRecord);
  CHECK_EQ(Save::LoadInt("best_time", -1), best - 300);
  CHECK_EQ(Save::LoadInt("best_coins", -1), 19);
  log::removeSink(&log);
  // Play again: New Game from the results resets the run.
  CHECK(SceneManager::LoadScene(static_cast<i32>(kResults)));
  g.run(3);
  CHECK(focused("AgainButton"));
  press(g, &VirtualGamepad::Buttons::south);
  g.run(2);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom));
  CHECK_EQ(GameState::coins, 0);
  CHECK_EQ(GameState::lives, GameState::kMaxLives);
  CHECK_EQ(GameState::ticks < 10, true);
  CHECK_EQ(Save::LoadInt("best_time", -1), best - 300);   // the best time survives a new game
}

TEST(flow_title_offers_continue_only_when_there_is_a_saved_game) {
  // No save: Continue is greyed out and skipped by the focus; South starts a new game.
  {
    Game g;
    CHECK(g.boot(cookSample(), tempDir("title_empty"), kTitle));
    g.run(3);
    GameObject* cont = GameObject::Find("ContinueButton");
    CHECK(cont != nullptr);
    CHECK(!cont->GetComponent<UIButton>()->interactable);
    CHECK(focused("NewButton"));
    g.pad.held.down = true;
    g.frame();
    g.pad.held.down = false;
    g.frame();
    CHECK(focused("NewButton"));   // there is nothing else to focus
    press(g, &VirtualGamepad::Buttons::south);
    g.run(2);
    CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom));
    CHECK_EQ(Save::LoadInt("room", -1), 1);   // the new game is saved on entering room 1
  }
  // A saved game in room 2: Continue is enabled and restores the totals.
  {
    fs::path dir = tempDir("title_saved");
    Game g;
    CHECK(g.boot(cookSample(), dir, kTitle));
    Save::SaveInt("room", 2);
    Save::SaveInt("coins", 9);
    Save::SaveInt("ticks", 3456);
    Save::SaveInt("lives", 2);
    Save::SaveInt("best_time", 5400);
    CHECK(SceneManager::LoadScene(static_cast<i32>(kTitle)));
    g.run(3);
    CHECK(GameObject::Find("ContinueButton")->GetComponent<UIButton>()->interactable);
    CHECK(strstr(GameObject::Find("ContinueLabel")->GetComponent<UIText>()->text, "ROOM 2") != nullptr);
    CHECK(strstr(GameObject::Find("TitleBest")->GetComponent<UIText>()->text, "1:30") != nullptr);   // 5400 ticks
    g.pad.held.down = true;
    g.frame();
    g.pad.held.down = false;
    g.frame();
    CHECK(focused("ContinueButton"));
    press(g, &VirtualGamepad::Buttons::south);
    g.run(2);
    CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom2));
    CHECK_EQ(GameState::coins, 9);
    CHECK_EQ(GameState::lives, 2);
    CHECK(GameState::ticks >= 3456 && GameState::ticks < 3456 + 10);
  }
}

TEST(flow_sample_starts_from_an_empty_save_in_every_run) {
  fs::path dir = tempDir("fresh");
  Game g;
  CHECK(g.boot(cookSample(), dir, kTitle));
  CHECK(!GameState::HasSave());
  CHECK_EQ(Save::LoadInt("best_time", -1), -1);
  CHECK_EQ(GameState::bestTicks, 0);
  CHECK(!fs::exists(dir / "room"));
}

TEST(flow_game_time_counts_fixed_steps_not_the_wall_clock) {
  Game a, b;
  CHECK(a.boot(cookSample(), tempDir("clockA"), kRoom));
  a.run(120);
  const i32 ta = GameState::ticks;
  CHECK(ta >= 118 && ta <= 120);
  // Fewer, longer frames cover the same game time: 60 frames of 1/30 s.
  CHECK(b.boot(cookSample(), tempDir("clockB"), kRoom));
  for (int i = 0; i < 60; ++i) b.loop.frame(1.0f / 30.0f, b.pad);
  CHECK(GameState::ticks >= 118 && GameState::ticks <= 120);
  char clock[16];
  GameState::FormatTime(clock, sizeof clock, 3600 + 25 * 60 + 59);
  CHECK_EQ(strcmp(clock, "1:25"), 0);
  GameState::FormatTime(clock, sizeof clock, 0);
  CHECK_EQ(strcmp(clock, "0:00"), 0);
}

TEST(flow_each_scene_picks_its_music_track) {
  static const struct { u32 scene; const char* track; } expect[] = {
      {kTitle, "music.wav"}, {kRoom, "music.wav"}, {kRoom2, "music2.wav"}, {kRoom3, "music3.wav"}, {kGameOver, ""}, {kResults, "music.wav"}};
  for (const auto& e : expect) {
    Game g;
    CHECK(g.boot(cookSample(), tempDir("music"), e.scene));
    g.run(3);
    CHECK_EQ(strcmp(GameState::track, e.track), 0);
  }
  // Moving from room 2 to room 3 switches the stream; staying in the same track does not restart it.
  Game g;
  CHECK(g.boot(cookSample(), tempDir("music2"), kRoom2));
  g.run(2);
  CHECK_EQ(strcmp(GameState::track, "music2.wav"), 0);
  GameState::PlayMusic("music2.wav");   // no restart: nothing changes
  CHECK_EQ(strcmp(GameState::track, "music2.wav"), 0);
  place("Player", 1440, 384);
  g.run(100);
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kRoom3));
  CHECK_EQ(strcmp(GameState::track, "music3.wav"), 0);
}
