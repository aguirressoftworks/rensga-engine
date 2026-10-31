// F4.01 exercise: the AutoPilot plays the WHOLE game (scene TitleBot) headless on the real GameLoop: it presses New Game, clears the
// three rooms (slope runs, one-way planks, enemies stomped, checkpoints, every coin) and ends on the Results scene. The pad it
// decided is recorded as an InputScript and replayed on the plain Room scene (no pilot): same coins, same game time, same stored best.
#include "rensga/Sim.h"
#include "sample_harness.h"

using namespace sample;

namespace {
constexpr u32 kMaxFrames = 60000;

struct Run {
  u32 frames = 0, roomStartFrame = 0;
  i32 coins = 0, ticks = 0, lives = 0, stomps = 0;
  i32 savedBest = -1, savedBestCoins = -1, savedRoom = -1;   // read from the save store before the game is torn down
  u32 scenesSeen[16] = {};
  u32 sceneCount = 0;
  bool finished = false;
};

// Plays TitleBot to the end. `dts` (optional) gives uneven frame times; `rec` records the pad from frame 0 (the bot's New Game press
// is recorded as a South press on the Title's focused button, so the script replays the whole run from the plain Title scene).
Run playGame(const fs::path& pack, const fs::path& saveDir, InputScript* rec, const Scalar* dts, u32 dtCount, Scalar dtScale, LogCapture* log) {
  Run run;
  Game g;
  if (!g.boot(pack, saveDir, kTitleBot)) return run;
  if (log) log::addSink(log);
  Scalar lastX = 0;
  u32 lastButtons = 0;
  u32 lastScene = 999;
  for (u32 f = 0; f < kMaxFrames; ++f) {
    const Scalar dt = dts ? dts[f % dtCount] * dtScale : 1.0f / 60.0f;
    if (!g.loop.frame(dt, g.pad)) { ++rtest::failures(); fprintf(stderr, "  heap allocation during frame %u\n", f); }
    ++run.frames;
    const u32 scene = static_cast<u32>(SceneManager::activeSceneIndex());
    if (scene != lastScene) {
      lastScene = scene;
      if (run.sceneCount < 16) run.scenesSeen[run.sceneCount++] = scene;
    }
    if (rec) {
      Scalar ax = GameState::botAxis;
      u32 buttons = GameState::botJump ? static_cast<u32>(InputScript::South) : 0u;
      if (f == 19) buttons = static_cast<u32>(InputScript::South);   // AutoPilot calls New Game in frame 19 (sceneFrames == 20)
      if (f == 0 || ax != lastX || buttons != lastButtons) {
        rec->add(f, ax, 0.0f, buttons);
        lastX = ax;
        lastButtons = buttons;
      }
    }
    if (scene == kResults && GameObject::Find("ResInit") != nullptr) {
      run.finished = true;
      for (int i = 0; i < 5; ++i) g.loop.frame(dt, g.pad);
      break;
    }
  }
  run.savedBest = Save::LoadInt("best_time", -1);
  run.savedBestCoins = Save::LoadInt("best_coins", -1);
  run.savedRoom = Save::LoadInt("room", -1);
  run.coins = GameState::finalCoins;
  run.ticks = GameState::finalTicks;
  run.lives = GameState::lives;
  run.stomps = GameState::stomps;
  if (log) log::removeSink(log);
  return run;
}
}  // namespace

TEST(platformer_bot_plays_the_whole_game_and_stores_the_best_time) {
  fs::path pack = cookSample();
  CHECK(!pack.empty());
  fs::path saveDir = tempDir("gamebot");
  InputScript recorded;
  LogCapture log;
  Run a;
  {
    a = playGame(pack, saveDir, &recorded, nullptr, 0, 1.0f, &log);
    CHECK(a.finished);
    CHECK_EQ(a.coins, GameState::kTotalCoins);
    CHECK_EQ(a.lives, GameState::kMaxLives);  // the pilot is never hit in a clean run
    CHECK(a.ticks > 600 && a.ticks < 20000);
    // Title -> Room -> Room2 -> Room3 -> Results, in this order.
    CHECK_EQ(a.sceneCount, 5u);
    if (a.sceneCount == 5) {
      CHECK_EQ(a.scenesSeen[0], kTitleBot);
      CHECK_EQ(a.scenesSeen[1], kRoom);
      CHECK_EQ(a.scenesSeen[2], kRoom2);
      CHECK_EQ(a.scenesSeen[3], kRoom3);
      CHECK_EQ(a.scenesSeen[4], kResults);
    }
    // The log lines the emulator runs and the CI scripts look for.
    CHECK(log.has("rensga: room cleared 1 "));
    CHECK(log.has("rensga: room cleared 2 "));
    CHECK(log.has("rensga: room cleared 3 "));
    CHECK(log.has("rensga: game complete coins=19 time="));
    CHECK_EQ(log.count("rensga: game complete"), 1);
    printf("    game bot: %u frames, game time %d ticks (%d s), coins %d, stomps %d\n", a.frames, a.ticks, GameState::Seconds(a.ticks), a.coins, a.stomps);
    // Saves: the best time was stored; the finished run leaves nothing to continue.
    CHECK_EQ(a.savedBest, a.ticks);
    CHECK_EQ(a.savedBestCoins, a.coins);
    CHECK_EQ(a.savedRoom, 0);
  }
  CHECK(recorded.count() > 0);
  // Replay the recorded pad from the plain Title scene (no pilot): the South press starts the game; deterministic, same result, and the
  // stored best stays (an equal time is not a new record).
  {
    Game g;
    CHECK(g.boot(pack, saveDir, kTitle));
    Input::setStickSettings(0.0f, 1.0f);  // the script holds raw stick values: no dead zone
    CHECK(GameObject::Find("AutoPilot") == nullptr);
    u32 f = 0;
    for (; f < a.frames && SceneManager::activeSceneIndex() != static_cast<i32>(kResults); ++f) {
      recorded.apply(f, g.pad);
      g.frame();
    }
    CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kResults));
    CHECK_EQ(GameState::finalCoins, a.coins);
    CHECK_EQ(GameState::finalTicks, a.ticks);
    CHECK(!GameState::newRecord);
    CHECK_EQ(Save::LoadInt("best_time", -1), a.ticks);
  }
}

// An emulator or a phone does not deliver exactly 1/60 s per frame: the pilot reacts to the world, so it still plays the game through.
TEST(platformer_bot_plays_the_whole_game_with_uneven_frame_times) {
  static const Scalar kDts[] = {0.0160f, 0.0172f, 0.0166f, 0.0210f, 0.0145f, 0.0167f, 0.0125f, 0.0190f};
  const fs::path pack = cookSample();
  CHECK(!pack.empty());
  for (int variant = 0; variant < 3; ++variant) {
    const Scalar scale = variant == 0 ? 1.0f : (variant == 1 ? 1.3f : 0.8f);
    Run r = playGame(pack, tempDir("gamebot_jitter"), nullptr, kDts, 8, scale, nullptr);
    CHECK(r.finished);
    CHECK_EQ(r.coins, GameState::kTotalCoins);
    printf("    jitter variant %d: complete after %u frames, game time %d, lives %d\n", variant, r.frames, r.ticks, r.lives);
  }
}

// A desktop player under a software GL runs 150-400 frames a second: several frames per fixed step, some with none.
TEST(platformer_bot_plays_the_whole_game_at_high_frame_rates) {
  static const Scalar kFast[] = {0.0027f, 0.0031f, 0.0042f, 0.0085f, 0.0024f, 0.0033f, 0.0029f, 0.0060f};
  const fs::path pack = cookSample();
  CHECK(!pack.empty());
  for (int variant = 0; variant < 2; ++variant) {
    Run r = playGame(pack, tempDir("gamebot_fast"), nullptr, kFast, 8, variant == 0 ? 1.0f : 0.6f, nullptr);
    CHECK(r.finished);
    CHECK_EQ(r.coins, GameState::kTotalCoins);
    printf("    fast variant %d: complete after %u frames, game time %d, lives %d\n", variant, r.frames, r.ticks, r.lives);
  }
}

// RoomBot = Room 1 plus the pilot: a way into the game that skips the title. The pilot survives the scene loads and plays on to the results.
TEST(platformer_roombot_scene_plays_from_room_1_to_the_results) {
  Game g;
  CHECK(g.boot(cookSample(), tempDir("roombot"), kRoomBot));
  CHECK(GameObject::Find("AutoPilot") != nullptr);
  u32 f = 0;
  for (; f < kMaxFrames && SceneManager::activeSceneIndex() != static_cast<i32>(kResults); ++f) g.frame();
  CHECK_EQ(SceneManager::activeSceneIndex(), static_cast<i32>(kResults));
  CHECK_EQ(GameState::finalCoins, GameState::kTotalCoins);
  printf("    room bot: results after %u frames, game time %d\n", f, GameState::finalTicks);
}
