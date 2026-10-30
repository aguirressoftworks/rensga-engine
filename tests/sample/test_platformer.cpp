// F2.05 exercise: a bot plays the one-room platformer headless (title -> start button -> collect all coins),
// records its pad input as an InputScript, and the script is replayed on a fresh world.
#include "rensga/Sim.h"
#include "sample_harness.h"

using namespace sample;

namespace {

// Chooses this frame's pad from the world state (a cheating bot: it reads object positions).
void botPad(Game& g, VirtualGamepad& pad) {
  pad = VirtualGamepad();
  GameObject* player = GameObject::Find("Player");
  if (!player) {  // title screen: press the start button once
    pad.held.south = g.frames == 4;
    return;
  }
  BoxCollider2D* box = player->GetComponent<BoxCollider2D>();
  if (!box || GameState::won) return;
  GameObject* target = nullptr;
  for (int i = 1; i <= 5 && !target; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "Coin%d", i);
    target = GameObject::Find(name);
  }
  if (!target) return;
  const Rect b = box->bounds();
  const Vector3 t = target->transform.position();
  const Scalar dx = (t.x + 8.0f) - (b.x + b.width * 0.5f), dy = (t.y + 8.0f) - (b.y + b.height * 0.5f);
  if (dx > 4.0f) pad.leftX = 1.0f;
  else if (dx < -4.0f) pad.leftX = -1.0f;
  bool jump = box->grounded && dy < -20.0f && Mathf::Abs(dx) < 110.0f;
  // A wall tile right ahead at foot height: hop onto it.
  GameObject* map = GameObject::Find("Tilemap");
  Tilemap* tm = map ? map->GetComponent<Tilemap>() : nullptr;
  if (tm && box->grounded && pad.leftX != 0.0f) {
    const Scalar ahead = pad.leftX > 0 ? b.x + b.width + 2.0f : b.x - 2.0f;
    if (tm->IsSolidCell(static_cast<i32>(Mathf::Floor(ahead / 32.0f)), static_cast<i32>(Mathf::Floor((b.y + b.height - 4.0f) / 32.0f)), true)) jump = true;
  }
  if (jump) pad.held.south = true;
}

}  // namespace

TEST(platformer_bot_collects_all_coins_and_saves_best_score) {
  fs::path pack = cookSample();
  CHECK(!pack.empty());
  fs::path saveDir = tempDir("platformer");
  InputScript recorded;
  i32 scoreA = 0;
  u32 framesToWin = 0;
  {
    Game g;
    CHECK(g.boot(pack, saveDir));
    CHECK(Save::LoadInt("best", -1) == -1);  // no save yet
    Scalar lastX = 0;
    u32 lastButtons = 0;
    bool inRoom = false;
    for (u32 f = 0; f < 3000 && !GameState::won; ++f) {
      botPad(g, g.pad);
      u32 buttons = g.pad.held.south ? static_cast<u32>(InputScript::South) : 0u;
      if (f == 0 || g.pad.leftX != lastX || buttons != lastButtons) {  // the script is piecewise constant
        CHECK(recorded.add(f, g.pad.leftX, g.pad.leftY, buttons));
        lastX = g.pad.leftX;
        lastButtons = buttons;
      }
      g.frame();
      inRoom = GameObject::Find("Player") != nullptr;
    }
    CHECK(inRoom);
    CHECK(GameState::won);
    CHECK_EQ(GameState::coins, GameState::kTotalCoins);
    CHECK(GameState::score > 0);
    scoreA = GameState::score;
    framesToWin = g.frames;
    g.run(3);
    CHECK_EQ(Save::LoadInt("best", -1), scoreA);
    // The save is a real file in the save root.
    CHECK(fs::exists(saveDir / "best"));
    printf("    platformer: won after %u frames, score %d\n", framesToWin, scoreA);
  }
  CHECK(recorded.count() > 0);
  // Replay: same input, same result (deterministic), and the best score only goes up.
  {
    Game g;
    CHECK(g.boot(pack, saveDir));
    for (u32 f = 0; f < framesToWin + 3; ++f) {
      recorded.apply(f, g.pad);
      g.frame();
    }
    CHECK(GameState::won);
    CHECK_EQ(GameState::score, scoreA);
    CHECK_EQ(Save::LoadInt("best", -1), scoreA);
  }
}
