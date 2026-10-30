// Shared state of the platformer sample. Plain statics: scripts talk to each other through them, and they survive scene loads.
#pragma once
#include <stdio.h>

#include <rensga/Audio.h>
#include <rensga/Rensga.h>

struct GameState {
  static constexpr rensga::i32 kTotalCoins = 7;
  static constexpr rensga::i32 kMaxHp = 3;
  static constexpr rensga::i32 kCoinScore = 100, kStompScore = 200;
  static inline rensga::i32 coins = 0;       // collected in the current run
  static inline rensga::i32 stomps = 0;      // enemies defeated
  static inline rensga::i32 hp = kMaxHp;
  static inline rensga::i32 fixedTicks = 0;  // fixed steps since the room started (deterministic clock)
  static inline bool won = false;
  static inline rensga::i32 score = 0;       // final score (set when the room is cleared)
  static inline rensga::i32 best = 0;        // best score, mirrored in the save file "best"
  static inline rensga::i32 saves = 0;       // save points touched in this run
  // Player <-> enemy requests, handled in the player's FixedUpdate.
  static inline rensga::i32 hurtRequest = 0;  // -1 / +1: hit from the left / right (knockback direction), 0 none
  static inline bool bounceRequest = false;   // a stomp: bounce up
  static inline rensga::Scalar playerVy = 0;  // the player's vertical speed after its last step (positive = falling)
  static inline rensga::i32 hurtTicks = 0;    // invulnerable for this many fixed steps after a hit
  // Last save point (respawn position of the player's Transform).
  static inline rensga::Scalar checkpointX = 64, checkpointY = 384;
  static inline bool respawnRequest = false;
  // Demo pilot (scene RoomBot): replaces the pad for the player while botActive.
  static inline bool botActive = false;
  static inline rensga::Scalar botAxis = 0;
  static inline bool botJump = false;
  // HUD message line.
  static inline char message[40] = {};
  static inline rensga::i32 messageTicks = 0;

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
  static void Message(const char* prefix, rensga::i32 value = -1) {
    if (value < 0) snprintf(message, sizeof(message), "%s", prefix);
    else snprintf(message, sizeof(message), "%s%d", prefix, static_cast<int>(value));
    messageTicks = 150;
  }
  // Score so far: coins and stomps (the time bonus is added when the room is cleared).
  static rensga::i32 RunScore() { return coins * kCoinScore + stomps * kStompScore; }
  // The save point (and the end of the room) stores the best score and the coin count through ISaveStore.
  static bool SaveGame() {
    if (RunScore() > best) best = RunScore();
    const bool a = rensga::Save::SaveInt("best", best);
    const bool b = rensga::Save::SaveInt("coins", coins);
    rensga::Debug::Log("rensga: saved best=%d coins=%d ok=%d", static_cast<int>(best), static_cast<int>(coins), a && b ? 1 : 0);
    return a && b;
  }
  static void StartRun() {
    coins = 0;
    stomps = 0;
    hp = kMaxHp;
    fixedTicks = 0;
    won = false;
    score = 0;
    saves = 0;
    hurtRequest = 0;
    bounceRequest = false;
    playerVy = 0;
    hurtTicks = 0;
    respawnRequest = false;
    checkpointX = 64;
    checkpointY = 384;
    message[0] = 0;
    messageTicks = 0;
  }
};
