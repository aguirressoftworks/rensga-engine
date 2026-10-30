#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Demo pilot for the scene RoomBot (the Room plus this object): it reads the world every frame and plays the room to the
// end, with the same moves a player makes: climbs the slope run, jumps the one-way planks, stomps both enemies, touches the
// save point and collects every coin. It only sets GameState::botAxis / botJump, which PlayerController2D reads instead of the
// pad. Used by the headless bot test and by the emulator runs (no controller needed).
struct AutoPilot : Behaviour {
  REFLECT(AutoPilot)

  enum Kind : u8 { kCoin, kStomp, kSave };
  // `via`: a point (centre x, y) to reach first while the player's feet are below viaFoot (the way up to a high coin).
  struct Step { Kind kind; const char* name; Scalar viaX, viaY, viaFoot; };
  static constexpr u32 kSteps = 10;

  i32 stage = 0;
  i32 viaDone = -1;

  static const Step* steps() {
    static const Step s[kSteps] = {{kCoin, "Coin1", 0, 0, 0},  {kCoin, "Coin2", 0, 0, 0}, {kStomp, "Walker1", 0, 0, 0}, {kCoin, "Coin3", 0, 0, 0},
                                   {kCoin, "Coin4", 620, 300, 330},  // plank B is out of reach from the floor: go up plank A first
                                   {kCoin, "Coin5", 0, 0, 0}, {kSave, "SavePoint", 0, 0, 0}, {kStomp, "Hopper1", 0, 0, 0}, {kCoin, "Coin6", 0, 0, 0},
                                   {kCoin, "Coin7", 0, 0, 0}};
    return s;
  }

  void Start() override {
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

  void Update() override {
    GameState::botAxis = 0;
    GameState::botJump = false;
    GameObject* player = GameObject::Find("Player");
    BoxCollider2D* pb = player ? player->GetComponent<BoxCollider2D>() : nullptr;
    if (!pb || GameState::won) return;
    while (stage < static_cast<i32>(kSteps)) {
      const Step& s = steps()[stage];
      const bool done = s.kind == kSave ? GameState::saves > 0 : GameObject::Find(s.name) == nullptr;
      if (!done) break;
      ++stage;
    }
    if (stage >= static_cast<i32>(kSteps)) return;
    const Step& s = steps()[stage];
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
        } else {
          GameState::botJump = true;
        }
      } else if (a.y + a.height < t.y - 2.0f) {  // above the enemy: fly over it and come down on it
        GameState::botAxis = Mathf::Clamp(dx / 12.0f, -1.0f, 1.0f);
      }
      return;
    }
    // Coin / save point: walk toward it, jump when it is above and near, hop onto a block that stands in the way.
    GameState::botAxis = Mathf::Abs(dx) > 4.0f ? Sign(dx) : 0.0f;
    bool jump = pb->grounded && dy < -20.0f && Mathf::Abs(dx) < 110.0f;
    GameObject* map = GameObject::Find("Tilemap");
    Tilemap* tm = map ? map->GetComponent<Tilemap>() : nullptr;
    if (tm && pb->grounded && GameState::botAxis != 0.0f) {
      const Scalar ahead = GameState::botAxis > 0 ? a.x + a.width + 2.0f : a.x - 2.0f;
      if (tm->IsSolidCell(static_cast<i32>(Mathf::Floor(ahead / 32.0f)), static_cast<i32>(Mathf::Floor((a.y + a.height - 4.0f) / 32.0f)), true)) jump = true;
    }
    GameState::botJump = jump;
  }
};
