// Shared state of the platformer sample. Plain statics: they survive scene loads (there is no DontDestroyOnLoad yet).
#pragma once
#include <rensga/Rensga.h>

struct GameState {
  static constexpr rensga::i32 kTotalCoins = 5;
  static inline rensga::i32 coins = 0;       // collected in the current run
  static inline rensga::i32 fixedTicks = 0;  // fixed steps since the room started (deterministic clock)
  static inline bool won = false;
  static inline rensga::i32 score = 0;
  static inline rensga::i32 best = 0;        // best score, mirrored in the save file "best"
  static void StartRun() {
    coins = 0;
    fixedTicks = 0;
    won = false;
    score = 0;
  }
};
