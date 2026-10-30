#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// A trigger box: the player's solid box entering it collects the coin.
struct Coin : Behaviour {
  REFLECT(Coin)
  void OnTriggerEnter(GameObject* other) override {
    if (!other || !other->CompareTag("Player")) return;
    ++GameState::coins;
    GameState::Sfx(GameState::kSfxCoin);
    Destroy(gameObject());
  }
};
