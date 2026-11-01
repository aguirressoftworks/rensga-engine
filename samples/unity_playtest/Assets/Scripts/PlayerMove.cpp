#include <rensga/Rensga.h>

#include "CoinGame.h"
#include "GameTuning.h"

using namespace rensga;

// Moves with the nub (Move action). With `bot` on, it walks to the nearest coin by itself (an emulator has no pad).
struct PlayerMove : Behaviour {
  REFLECT(PlayerMove)
  SERIALIZE_FIELD(Guid, tuning, Guid())
  SERIALIZE_FIELD(bool, bot, false)
  RANGE(0, 12)
  SERIALIZE_FIELD(Scalar, speedFallback, 5.0f)

  const GameTuning* t = nullptr;
  Vector2 stick;

  void Start() override { t = DataAssets::Get<GameTuning>(tuning); }

  GameObject* nearestCoin() {
    GameObject* best = nullptr;
    Scalar bestD = 1e9f;
    GameObject* coins[32];
    const u32 n = GameObject::FindGameObjectsWithTag("Coin", coins, 32);
    for (u32 i = 0; i < n; ++i) {
      if (!coins[i] || !coins[i]->activeInHierarchy()) continue;
      const Scalar d = (coins[i]->transform.position() - transform()->position()).sqrMagnitude();
      if (d < bestD) { bestD = d; best = coins[i]; }
    }
    return best;
  }

  void FixedUpdate() override {
    CharacterController* cc = gameObject()->GetComponent<CharacterController>();
    if (!cc) return;
    stick = Input::GetAxis2("Move");
    if (bot) {
      stick = Vector2(0, 0);
      if (GameObject* c = nearestCoin()) {
        Vector3 d = c->transform.position() - transform()->position();
        d.y = 0;
        if (d.sqrMagnitude() > 0.01f) { d = d.normalized(); stick = Vector2(d.x, -d.z); }
      }
    }
    const Scalar speed = t ? t->playerSpeed : speedFallback;
    // the camera looks down -z of the world from +z: stick.y is "forward" = -z
    Vector3 dir(stick.x, 0, -stick.y);
    if (dir.sqrMagnitude() > 1.0f) dir = dir.normalized();
    cc->Move(dir * (speed * Time::fixedDeltaTime()));
  }
};
