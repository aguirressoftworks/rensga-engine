// Shared state of the platformer sample. Plain statics: they survive scene loads (there is no DontDestroyOnLoad yet).
#pragma once
#include <rensga/Audio.h>
#include <rensga/Rensga.h>

struct GameState {
  static constexpr rensga::i32 kTotalCoins = 5;
  static inline rensga::i32 coins = 0;       // collected in the current run
  static inline rensga::i32 fixedTicks = 0;  // fixed steps since the room started (deterministic clock)
  static inline bool won = false;
  static inline rensga::i32 score = 0;
  static inline rensga::i32 best = 0;        // best score, mirrored in the save file "best"
  // Sound effect GUIDs (Assets/Audio/*.wav.meta). Silent when no mixer is running (headless tests).
  static constexpr const char* kSfxJump = "00000000-0000-4000-8000-000000000201";
  static constexpr const char* kSfxCoin = "00000000-0000-4000-8000-000000000202";
  static constexpr const char* kSfxHit = "00000000-0000-4000-8000-000000000203";
  static constexpr const char* kSfxSave = "00000000-0000-4000-8000-000000000204";
  static void Sfx(const char* guid) {
    rensga::Guid g;
    rensga::AudioMixer* m = rensga::AudioSystem::mixer();
    if (!m || !rensga::Guid::parse(guid, g)) return;
    if (const rensga::AudioClip* c = rensga::AudioSystem::findClip(g)) m->play(c, 0.8f);
  }
  static void StartRun() {
    coins = 0;
    fixedTicks = 0;
    won = false;
    score = 0;
  }
};
