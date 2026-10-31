// Shared state and flow of the platformer sample. Plain statics: scripts talk to each other through them, and they survive scene loads.
//
// The game: Title -> Room (room 1) -> Room2 -> Room3 -> Results; GameOver on the third lost life. All times are GAME time (fixed
// steps run by the player while a room is in play), never the wall clock, so a run is the same on every target.
// Saves (Save::SaveInt): room (the room to continue in, 0 = none), coins and ticks (totals when that room was entered), lives,
// best_time (game ticks of the fastest finished run, 0 = none) and best_coins. A new project starts with an empty save.
#pragma once
#include <stdio.h>
#include <string.h>

#include <rensga/Audio.h>
#include <rensga/Rensga.h>

struct GameState {
  static constexpr rensga::i32 kRooms = 3;
  static constexpr rensga::i32 kMaxHp = 3, kMaxLives = 3;
  // Coins placed in each room. tests/sample checks these against the generated scenes.
  static constexpr rensga::i32 kRoomCoins[kRooms] = {7, 6, 6};
  static constexpr rensga::i32 kTotalCoins = 19;

  // ---- run state (reset by NewGame / LoadProgress, kept between rooms) ----
  static inline rensga::i32 room = 1;        // 1..kRooms
  static inline rensga::i32 coins = 0;       // collected in the whole run
  static inline rensga::i32 roomCoins = 0;   // collected in the current room
  static inline rensga::i32 stomps = 0;      // enemies defeated in the whole run
  static inline rensga::i32 lives = kMaxLives;
  static inline rensga::i32 hp = kMaxHp;
  static inline rensga::i32 ticks = 0;       // game time: fixed steps in play (the pause and the transitions do not count)
  // Snapshot taken when the current room was entered: "Continue" after a game over restarts the room from it.
  static inline rensga::i32 entryCoins = 0, entryTicks = 0;
  static inline rensga::i32 saves = 0;       // checkpoints touched in this room
  // Room flow. 0 playing, 1 room exit reached, 2 goal reached (game complete), 3 out of lives. HudController runs the transition.
  static inline rensga::i32 exitRequest = 0;
  static inline bool paused = false;
  static inline rensga::u64 resumeFrame = ~0ull;   // frame of the last Resume press: the player ignores the pad in that frame (South = Resume, not Jump)
  // Result of the last finished run (Results scene).
  static inline rensga::i32 finalTicks = 0, finalCoins = 0, bestTicks = 0, bestCoins = 0;
  static inline bool newRecord = false;

  // Player <-> enemy requests, handled in the player's FixedUpdate.
  static inline rensga::i32 hurtRequest = 0;  // -1 / +1: hit from the left / right (knockback direction), 0 none
  static inline bool bounceRequest = false;   // a stomp: bounce up
  static inline rensga::Scalar playerVy = 0;  // the player's vertical speed after its last step (positive = falling)
  static inline rensga::i32 hurtTicks = 0;    // invulnerable for this many fixed steps after a hit
  // Last checkpoint (respawn position of the player's Transform).
  static inline rensga::Scalar checkpointX = 64, checkpointY = 384;
  static inline bool respawnRequest = false;
  // Demo pilot (AutoPilot): replaces the pad for the player while botActive.
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
  static constexpr const char* kSfxLose = "00000000-0000-4000-8000-000000000205";
  static constexpr const char* kSfxClear = "00000000-0000-4000-8000-000000000206";
  static constexpr const char* kSfxSelect = "00000000-0000-4000-8000-000000000207";
  static constexpr const char* kSfxOver = "00000000-0000-4000-8000-000000000208";
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

  // ---- music ----
  // The runtime starts "music.wav" at boot (one streamed voice). Each room switches the stream with AudioMixer::playMusic:
  // music.wav = title, room 1, results; music2.wav = room 2; music3.wav = room 3; the game over scene is silent. A file that is not
  // on the target (a platform package without the extra streamed files) falls back to music.wav. `track` mirrors what plays
  // (also when there is no mixer, as in the headless tests); the same track is never restarted.
  static inline const char* track = "music.wav";
  static constexpr const char* kRoomTrack[kRooms] = {"music.wav", "music2.wav", "music3.wav"};
  static void PlayMusic(const char* file) {
    if (!strcmp(track, file)) return;
    track = file;
    rensga::AudioMixer* m = rensga::AudioSystem::mixer();
    if (!m) return;
    char path[48];
    snprintf(path, sizeof path, "data:/%s", file);
    if (!m->playMusic(path, 1.0f, true) && strcmp(file, "music.wav") != 0) {
      track = "music.wav";
      m->playMusic("data:/music.wav", 1.0f, true);
    }
  }
  static void StopMusic() {
    track = "";
    if (rensga::AudioMixer* m = rensga::AudioSystem::mixer()) m->stopMusic();
  }

  // ---- time ----
  static rensga::i32 Hz() {
    const rensga::Scalar dt = rensga::Time::fixedDeltaTime();
    return dt > 0.0f ? static_cast<rensga::i32>(1.0f / dt + 0.5f) : 60;
  }
  static rensga::i32 Seconds(rensga::i32 t) { return t / Hz(); }
  // "m:ss" of a tick count.
  static void FormatTime(char* out, rensga::u32 cap, rensga::i32 t) {
    const rensga::i32 s = Seconds(t);
    snprintf(out, cap, "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
  }

  // ---- scene names ----
  static const char* RoomScene(rensga::i32 r) {
    static const char* const names[kRooms] = {"Room", "Room2", "Room3"};
    return names[(r < 1 ? 1 : (r > kRooms ? kRooms : r)) - 1];
  }

  // ---- saves ----
  static bool HasSave() { return rensga::Save::LoadInt("room", 0) > 0; }
  static void SaveProgress() {
    rensga::Save::SaveInt("room", room);
    rensga::Save::SaveInt("coins", entryCoins);
    rensga::Save::SaveInt("ticks", entryTicks);
    rensga::Save::SaveInt("lives", lives);
  }
  static void LoadBest() {
    bestTicks = rensga::Save::LoadInt("best_time", 0);
    bestCoins = rensga::Save::LoadInt("best_coins", 0);
  }

  // ---- flow ----
  static void ResetRoomState() {
    hurtRequest = 0;
    bounceRequest = false;
    playerVy = 0;
    hurtTicks = 0;
    respawnRequest = false;
    exitRequest = 0;
    paused = false;
    saves = 0;
    roomCoins = 0;
    message[0] = 0;
    messageTicks = 0;
    rensga::Time::setTimeScale(1.0f);
  }
  // The state of a fresh run, without loading a scene (tests and NewGame).
  static void ResetRun() {
    room = 1;
    coins = 0;
    stomps = 0;
    lives = kMaxLives;
    hp = kMaxHp;
    ticks = 0;
    entryCoins = 0;
    entryTicks = 0;
    newRecord = false;
    exitRequest = 0;
    checkpointX = 64;
    checkpointY = 384;
    ResetRoomState();
  }
  static void NewGame() {
    ResetRun();
    rensga::SceneManager::LoadScene(RoomScene(1));
  }
  static void ContinueGame() {
    room = rensga::Save::LoadInt("room", 1);
    if (room < 1 || room > kRooms) room = 1;
    coins = rensga::Save::LoadInt("coins", 0);
    ticks = rensga::Save::LoadInt("ticks", 0);
    lives = rensga::Save::LoadInt("lives", kMaxLives);
    if (lives < 1) lives = kMaxLives;
    stomps = 0;
    entryCoins = coins;
    entryTicks = ticks;
    newRecord = false;
    ResetRoomState();
    rensga::SceneManager::LoadScene(RoomScene(room));
  }
  // After a game over: the same room again from its entry, with full lives.
  static void RetryRoom() {
    lives = kMaxLives;
    coins = entryCoins;
    ticks = entryTicks;
    ResetRoomState();
    rensga::SceneManager::LoadScene(RoomScene(room));
  }
  static void ToTitle() {
    ResetRoomState();
    rensga::SceneManager::LoadScene("Title");
  }
  // Called by every room when it starts (HudController.Start).
  static void BeginRoom(rensga::i32 r) {
    room = r;
    hp = kMaxHp;
    ResetRoomState();
    entryCoins = coins;
    entryTicks = ticks;
    LoadBest();
    SaveProgress();
    rensga::Debug::Log("rensga: room %d start coins=%d ticks=%d lives=%d", static_cast<int>(room), static_cast<int>(coins),
                       static_cast<int>(ticks), static_cast<int>(lives));
  }
  // The player reached the exit of the current room.
  static void ReachExit() {
    if (exitRequest != 0) return;
    exitRequest = 1;
    Sfx(kSfxClear);
    rensga::Debug::Log("rensga: room cleared %d coins=%d room_coins=%d time=%d", static_cast<int>(room), static_cast<int>(coins),
                       static_cast<int>(roomCoins), static_cast<int>(ticks));
  }
  // The player reached the goal of the last room: the run is over, the best time is stored.
  static void ReachGoal() {
    if (exitRequest != 0) return;
    exitRequest = 2;
    Sfx(kSfxClear);
    finalTicks = ticks;
    finalCoins = coins;
    LoadBest();
    newRecord = bestTicks <= 0 || finalTicks < bestTicks;
    if (newRecord) {
      bestTicks = finalTicks;
      bestCoins = finalCoins;
      rensga::Save::SaveInt("best_time", bestTicks);
      rensga::Save::SaveInt("best_coins", bestCoins);
    }
    rensga::Save::SaveInt("room", 0);  // the run is finished: nothing to continue
    rensga::Debug::Log("rensga: room cleared %d coins=%d room_coins=%d time=%d", static_cast<int>(room), static_cast<int>(coins),
                       static_cast<int>(roomCoins), static_cast<int>(ticks));
    rensga::Debug::Log("rensga: game complete coins=%d time=%d seconds=%d best=%d record=%d", static_cast<int>(finalCoins),
                       static_cast<int>(finalTicks), static_cast<int>(Seconds(finalTicks)), static_cast<int>(bestTicks), newRecord ? 1 : 0);
  }
  static bool Frozen() { return exitRequest != 0; }
};
