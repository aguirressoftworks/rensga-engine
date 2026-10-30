// F2.05 exercise: the AutoPilot plays the room (scene RoomBot) headless on the real GameLoop: slope run, one-way planks, both
// enemies stomped, save point touched, every coin collected, "room cleared". Its pad is recorded as an InputScript and replayed on
// the plain Room scene (no pilot): same score, same stored best.
#include "rensga/Sim.h"
#include "sample_harness.h"

using namespace sample;

namespace {
constexpr u32 kRoom = 1, kRoomBot = 2, kMaxFrames = 7000;
}

TEST(platformer_bot_clears_the_room_and_stores_the_best_score) {
  fs::path pack = cookSample();
  CHECK(!pack.empty());
  fs::path saveDir = tempDir("roombot");
  InputScript recorded;
  i32 scoreA = 0;
  u32 framesToWin = 0;
  {
    Game g;
    CHECK(g.boot(pack, saveDir, kRoomBot));
    CHECK(GameObject::Find("AutoPilot") != nullptr);
    CHECK(Save::LoadInt("best", -1) == -1);  // no save yet
    Scalar lastX = 0;
    u32 lastButtons = 0;
    bool sawPlateau = false, sawPlankA = false, sawPlankB = false, savedMid = false;
    Scalar maxView = 0;
    for (u32 f = 0; f < kMaxFrames && !GameState::won; ++f) {
      g.frame();
      // Record this frame's pad (the pilot decided it in Update; the script is piecewise constant).
      const Scalar ax = GameState::botAxis;
      const u32 buttons = GameState::botJump ? static_cast<u32>(InputScript::South) : 0u;
      if (f == 0 || ax != lastX || buttons != lastButtons) {
        CHECK(recorded.add(f, ax, 0.0f, buttons));
        lastX = ax;
        lastButtons = buttons;
      }
      GameObject* player = GameObject::Find("Player");
      BoxCollider2D* box = player ? player->GetComponent<BoxCollider2D>() : nullptr;
      if (box && box->grounded) {
        const Rect b = box->bounds();
        const Scalar foot = b.y + b.height, cx = b.x + b.width * 0.5f;
        if (foot > 345.0f && foot < 356.0f && cx > 330.0f && cx < 410.0f) sawPlateau = true;  // reached the top of the slope run
        if (foot > 318.0f && foot < 322.0f && cx > 544.0f && cx < 672.0f) sawPlankA = true;   // stood on one-way plank A
        if (foot > 254.0f && foot < 258.0f && cx > 704.0f && cx < 832.0f) sawPlankB = true;   // and on plank B
      }
      if (Camera2D* cam = Camera2D::main()) maxView = Mathf::Max(maxView, cam->viewX);
      if (GameState::saves == 1 && !savedMid) {  // the save point stored the score so far, before the room was cleared
        savedMid = true;
        CHECK_EQ(Save::LoadInt("best", -1), GameState::RunScore());
        CHECK_EQ(Save::LoadInt("coins", -1), GameState::coins);
        CHECK(GameState::coins >= 4 && GameState::coins < GameState::kTotalCoins);
        CHECK_EQ(GameState::stomps, 1);
      }
    }
    CHECK(GameState::won);
    CHECK_EQ(GameState::coins, GameState::kTotalCoins);
    CHECK_EQ(GameState::stomps, 2);
    CHECK_EQ(GameState::hp, GameState::kMaxHp);  // the pilot is never hit
    CHECK(GameObject::Find("Walker1") == nullptr);
    CHECK(GameObject::Find("Hopper1") == nullptr);
    CHECK(savedMid);
    CHECK(sawPlateau);
    CHECK(sawPlankA);
    CHECK(sawPlankB);
    CHECK(maxView > 500.0f);  // the camera scrolled across the wide room
    CHECK(GameState::score >= GameState::RunScore());
    scoreA = GameState::score;
    framesToWin = g.frames;
    g.run(3);
    CHECK_EQ(Save::LoadInt("best", -1), scoreA);
    CHECK_EQ(Save::LoadInt("coins", -1), GameState::kTotalCoins);
    CHECK(fs::exists(saveDir / "best"));
    printf("    room bot: cleared after %u frames, score %d\n", framesToWin, scoreA);
    UIText* msg = GameObject::Find("HudMessage")->GetComponent<UIText>();
    CHECK(strstr(msg->text, "ROOM CLEARED") != nullptr);
  }
  CHECK(recorded.count() > 0);
  // Replay the recorded pad on the plain Room: deterministic, and the stored best does not go down.
  {
    Game g;
    CHECK(g.boot(pack, saveDir, kRoom));
    Input::setStickSettings(0.0f, 1.0f);  // the script holds raw stick values: no dead zone
    CHECK(GameObject::Find("AutoPilot") == nullptr);
    for (u32 f = 0; f < framesToWin + 3; ++f) {
      recorded.apply(f, g.pad);
      g.frame();
    }
    CHECK(GameState::won);
    CHECK_EQ(GameState::score, scoreA);
    CHECK_EQ(Save::LoadInt("best", -1), scoreA);
  }
  // A fresh save slot gets the same result from the replay.
  {
    fs::path other = tempDir("roombot2");
    Game g;
    CHECK(g.boot(pack, other, kRoom));
    Input::setStickSettings(0.0f, 1.0f);
    for (u32 f = 0; f < framesToWin + 3; ++f) {
      recorded.apply(f, g.pad);
      g.frame();
    }
    CHECK(GameState::won);
    CHECK_EQ(GameState::score, scoreA);
    CHECK_EQ(Save::LoadInt("best", -1), scoreA);
  }
}

// An emulator or a phone does not deliver exactly 1/60 s per frame: the pilot reacts to the world, so it still clears the room.
TEST(platformer_bot_clears_the_room_with_uneven_frame_times) {
  static const Scalar kDts[] = {0.0160f, 0.0172f, 0.0166f, 0.0210f, 0.0145f, 0.0167f, 0.0125f, 0.0190f};
  const fs::path pack = cookSample();
  CHECK(!pack.empty());
  for (int variant = 0; variant < 3; ++variant) {
    Game g;
    CHECK(g.boot(pack, tempDir("roombot_jitter"), kRoomBot));
    const Scalar scale = variant == 0 ? 1.0f : (variant == 1 ? 1.3f : 0.8f);
    u32 f = 0;
    for (; f < 12000 && !GameState::won; ++f) {
      CHECK(g.loop.frame(kDts[f % 8] * scale, g.pad));
    }
    CHECK(GameState::won);
    CHECK_EQ(GameState::coins, GameState::kTotalCoins);
    CHECK_EQ(GameState::stomps, 2);
    printf("    jitter variant %d: cleared after %u frames, hp %d, score %d\n", variant, f, static_cast<int>(GameState::hp), static_cast<int>(GameState::score));
  }
}
