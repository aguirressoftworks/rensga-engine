#include <string.h>

#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Demo pilot that plays the whole game with no controller (scene TitleBot = the Title plus this object, which survives scene loads).
// It presses New Game, then reads the world every frame and plays each room to its exit with the same moves a player makes: climbs
// slopes, jumps the one-way planks, stomps the enemies, touches the checkpoint, collects every coin and walks into the exit. It only sets
// GameState::botAxis / botJump, which PlayerController2D reads instead of the pad. After a game over it chooses Continue.
// Used by the headless bot test and by emulator runs (no pad there). Logs "room cleared <n>" and "game complete coins=<c> time=<t>".
struct AutoPilot : Behaviour {
  REFLECT(AutoPilot)
  TOOLTIP("Automated runs: quit the application this many frames after the Results scene appeared (0 = keep running).")
  RANGE(0, 100000)
  SERIALIZE_FIELD(i32, quitAfterResultsFrames, 0)

  enum Kind : u8 { kCoin, kStomp, kSave, kExit };
  // `via`: a point (centre x, y) to reach first while the player's feet are below viaFoot (the way up to a high coin).
  struct Step { Kind kind; const char* name; Scalar viaX, viaY, viaFoot; };

  static constexpr u32 kMaxSteps = 16;
  struct Plan { u32 count; Step steps[kMaxSteps]; };

  static const Plan& PlanFor(i32 room) {
    static const Plan p1 = {11, {{kCoin, "Coin1", 0, 0, 0}, {kCoin, "Coin2", 0, 0, 0}, {kStomp, "Walker1", 0, 0, 0}, {kCoin, "Coin3", 0, 0, 0},
                                 {kCoin, "Coin4", 620, 300, 330},  // plank B is out of reach from the floor: go up plank A first
                                 {kCoin, "Coin5", 0, 0, 0}, {kSave, "SavePoint", 0, 0, 0}, {kStomp, "Hopper1", 0, 0, 0}, {kCoin, "Coin6", 0, 0, 0},
                                 {kCoin, "Coin7", 0, 0, 0}, {kExit, "Exit", 0, 0, 0}}};
    static const Plan p2 = {10, {{kCoin, "Coin1", 0, 0, 0}, {kStomp, "Hopper1", 0, 0, 0}, {kCoin, "Coin2", 0, 0, 0}, {kSave, "SavePoint", 0, 0, 0},
                                 {kCoin, "Coin3", 0, 0, 0}, {kCoin, "Coin4", 0, 0, 0}, {kCoin, "Coin5", 0, 0, 0}, {kStomp, "Hopper2", 0, 0, 0},
                                 {kCoin, "Coin6", 0, 0, 0}, {kExit, "Exit", 0, 0, 0}}};
    static const Plan p3 = {14, {{kStomp, "Walker1", 0, 0, 0}, {kCoin, "Coin1", 0, 0, 0}, {kCoin, "Coin2", 0, 0, 0}, {kStomp, "Hopper1", 0, 0, 0},
                                 {kCoin, "Coin3", 0, 0, 0}, {kSave, "SavePoint", 0, 0, 0}, {kStomp, "Walker2", 0, 0, 0}, {kCoin, "Coin4", 0, 0, 0},
                                 {kCoin, "Coin5", 0, 0, 0}, {kStomp, "Hopper2", 0, 0, 0}, {kStomp, "Walker3", 0, 0, 0}, {kCoin, "Coin6", 0, 0, 0},
                                 {kExit, "Goal", 0, 0, 0}}};
    return room <= 1 ? p1 : (room == 2 ? p2 : p3);
  }

  i32 stage = 0;
  i32 viaDone = -1;
  i32 scene = -1;
  i32 sceneFrames = 0;
  bool reported = false;

  void Start() override {
    DontDestroyOnLoad(gameObject());
    GameState::botActive = true;
    GameState::botAxis = 0;
    GameState::botJump = false;
    stage = 0;
  }
  void OnDestroy() override {
    GameState::botActive = false;
    GameState::botAxis = 0;
    GameState::botJump = false;
  }

  static Scalar Sign(Scalar v) { return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f); }

  // A solid tile in the way of a walking player (grounded, heading `dir`): a block to hop onto.
  static bool BlockAhead(const Rect& a, Scalar dir) {
    GameObject* map = GameObject::Find("Tilemap");
    Tilemap* tm = map ? map->GetComponent<Tilemap>() : nullptr;
    if (!tm || dir == 0.0f) return false;
    const Scalar ahead = dir > 0 ? a.x + a.width + 2.0f : a.x - 2.0f;
    return tm->IsSolidCell(static_cast<i32>(Mathf::Floor(ahead / 32.0f)), static_cast<i32>(Mathf::Floor((a.y + a.height - 4.0f) / 32.0f)), true);
  }

  void Update() override {
    GameState::botAxis = 0;
    GameState::botJump = false;
    const i32 now = SceneManager::activeSceneIndex();
    if (now != scene) {
      scene = now;
      sceneFrames = 0;
      stage = 0;
      viaDone = -1;
    }
    ++sceneFrames;
    const char* name = SceneManager::activeSceneName();
    if (!strcmp(name, "Title") || !strcmp(name, "TitleBot")) {
      if (sceneFrames == 20) GameState::NewGame();
      return;
    }
    if (!strcmp(name, "GameOver")) {
      if (sceneFrames == 40) GameState::RetryRoom();
      return;
    }
    if (!strcmp(name, "Results")) {
      if (!reported) {
        reported = true;
        Debug::Log("rensga: bot finished the game coins=%d time=%d", static_cast<int>(GameState::finalCoins), static_cast<int>(GameState::finalTicks));
      }
      if (quitAfterResultsFrames > 0 && sceneFrames >= quitAfterResultsFrames) Application::Quit();
      return;
    }
    GameObject* player = GameObject::Find("Player");
    BoxCollider2D* pb = player ? player->GetComponent<BoxCollider2D>() : nullptr;
    if (!pb || GameState::Frozen() || GameState::paused || sceneFrames < 4) return;   // (the room's own scripts start in its first frames)
    // The room comes from the scene name, not from GameState::room: HudController.Start sets that one, and which script runs first in the
    // first frame of a room is not defined.
    const Plan& plan = PlanFor(!strcmp(name, "Room3") ? 3 : (!strcmp(name, "Room2") ? 2 : 1));
    while (stage < static_cast<i32>(plan.count)) {
      const Step& s = plan.steps[stage];
      const bool done = s.kind == kSave ? GameState::saves > 0 : (s.kind == kExit ? false : GameObject::Find(s.name) == nullptr);
      if (!done) break;
      ++stage;
    }
    if (stage >= static_cast<i32>(plan.count)) return;
    const Step& s = plan.steps[stage];
    GameObject* target = GameObject::Find(s.name);
    BoxCollider2D* tb = target ? target->GetComponent<BoxCollider2D>() : nullptr;
    if (!tb) return;
    const Rect a = pb->bounds(), t = tb->bounds();
    const Scalar pcx = a.x + a.width * 0.5f, tcx = t.x + t.width * 0.5f;
    Scalar tx = tcx, ty = t.y + t.height * 0.5f;
    if (s.viaFoot > 0.0f) {  // latch: once standing up there, aim for the coin; back on the floor, go up again
      if (pb->grounded) viaDone = a.y + a.height <= s.viaFoot ? stage : (a.y + a.height > s.viaFoot + 40.0f ? -1 : viaDone);
      if (viaDone != stage) { tx = s.viaX; ty = s.viaY; }
    }
    const Scalar dx = tx - pcx, dy = ty - (a.y + a.height * 0.5f);
    if (s.kind == kStomp) {
      if (pb->grounded) {
        if (Mathf::Abs(dx) > 100.0f) {
          GameState::botAxis = Sign(dx);
          GameState::botJump = BlockAhead(a, GameState::botAxis);   // a pillar between the pilot and the enemy: hop onto it
        } else {
          GameState::botJump = true;
        }
      } else if (a.y + a.height < t.y - 2.0f) {  // above the enemy: fly over it and come down on it
        GameState::botAxis = Mathf::Clamp(dx / 12.0f, -1.0f, 1.0f);
      }
      return;
    }
    // Coin / checkpoint / exit: walk toward it, jump when it is above and near, hop onto a block that stands in the way.
    GameState::botAxis = Mathf::Abs(dx) > 4.0f ? Sign(dx) : 0.0f;
    bool jump = pb->grounded && dy < -20.0f && Mathf::Abs(dx) < 110.0f;
    if (pb->grounded && BlockAhead(a, GameState::botAxis)) jump = true;
    GameState::botJump = jump;
  }
};
