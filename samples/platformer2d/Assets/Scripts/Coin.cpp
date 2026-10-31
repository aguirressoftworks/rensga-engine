#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// A trigger box: the player's solid box entering it collects the coin.
struct Coin : Behaviour {
  REFLECT(Coin)
  void OnTriggerEnter(GameObject* other) override {
    if (!other || !other->CompareTag("Player") || GameState::Frozen()) return;
    ++GameState::coins;
    ++GameState::roomCoins;
    GameState::Sfx(GameState::kSfxCoin);
    Destroy(gameObject());
  }
};
