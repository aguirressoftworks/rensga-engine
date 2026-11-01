// The score, shared by Coin and CoinGame (the same pattern as the template's PickupCounter).
#pragma once
#include <rensga/Rensga.h>

struct CoinScore {
  static inline rensga::i32 score = 0, collected = 0, spawned = 0;
  static void Collect(rensga::i32 value) {
    score += value;
    ++collected;
    rensga::Debug::Log("rensga: coin collected %d/%d score=%d", static_cast<int>(collected), static_cast<int>(spawned), static_cast<int>(score));
  }
};
