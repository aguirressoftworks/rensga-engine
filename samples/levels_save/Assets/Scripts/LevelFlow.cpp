#include <rensga/Rensga.h>
#include <stdio.h>
#include <string.h>

using namespace rensga;

// F6.11 levels_save: two levels behind an asynchronous loading screen, and a save that survives a restart.
//   Menu (scene 0)  -> reads slot 0 ("continue") or starts a new game, then
//   Loading         -> loaded ADDITIVELY on top of the menu: a bar that follows the AsyncOperation of the level,
//   Level1 / Level2 -> loaded asynchronously in Single mode (the loading overlay and the menu go away with the old scene; this
//                      object survives through DontDestroyOnLoad), then
//   Hud             -> loaded asynchronously in Additive mode next to the level; it shows level and coins.
// The bot plays ONE level per run (about 100 frames), writes SaveSystem slot 0 (version 2) and quits. The next run continues from
// the save, so "play, close the emulator, play again" is the exercise. PlayerPrefs counts the runs. Every step logs a
// "rensga: levels: ..." line that tools/ci/levels_save.sh checks.
struct LevelFlow : Behaviour {
  REFLECT(LevelFlow)
  TOOLTIP("Frames the loading screen stays up even when the level is ready, so a person can see the bar.")
  SERIALIZE_FIELD(i32, minLoadFrames, 90)
  TOOLTIP("Work items one asynchronous load step does per frame (SceneManager::setAsyncChunk). A small number stretches the load.")
  SERIALIZE_FIELD(i32, chunk, 2)
  TOOLTIP("What happens once a level is saved: 0 quit (the restart exercise), 1 idle and keep running (an emulator that is stopped from outside), 2 play on through every level.")
  SERIALIZE_FIELD(i32, afterSave, 0)
  TOOLTIP("Frames the menu waits before it starts (a person reads the continue line).")
  SERIALIZE_FIELD(i32, menuFrames, 40)

  // Save layout: version 1 had {level, coins}, version 2 adds lives. Read migrates an old file.
  struct SaveData { i32 level; i32 coins; i32 lives; };
  struct SaveDataV1 { i32 level; i32 coins; };
  static constexpr u32 kSaveVersion = 2;
  static constexpr i32 kLevels = 2;

  enum State { Menu, Loading, Playing, Finished };
  State state = Menu;
  SaveData data = {1, 0, 3};
  AsyncOperation op, hudOp;
  i32 target = 1;
  i32 frames = 0, loadFrames = 0, playFrames = 0;
  bool hudStarted = false, levelDone = false;

  static bool Migrate(u32 from, const void* in, u32 inSize, void* out, u32 cap, u32* outSize) {
    if (from != 1 || inSize != sizeof(SaveDataV1) || cap < sizeof(SaveData)) return false;
    SaveDataV1 old;
    memcpy(&old, in, sizeof old);
    SaveData n = {old.level, old.coins, 3};
    memcpy(out, &n, sizeof n);
    *outSize = sizeof n;
    return true;
  }

  static const char* LevelName(i32 level) { return level == 1 ? "Level1" : "Level2"; }

  static void SetText(const char* object, const char* text) {
    if (GameObject* o = GameObject::Find(object))
      if (UIText* t = o->GetComponent<UIText>()) t->SetText(text);
  }

  void Start() override {
    SceneManager::setAsyncChunk(static_cast<u32>(chunk));
    DontDestroyOnLoad(gameObject());
    const i32 runs = PlayerPrefs::GetInt("runs", 0) + 1;
    PlayerPrefs::SetInt("runs", runs);
    PlayerPrefs::Save();
    Debug::Log("rensga: levels: prefs runs=%d", runs);
    Handle<ResAudioClip> blip = Resources::Load<ResAudioClip>("sfx/blip");
    Debug::Log("rensga: levels: resources blip valid=%d entries=%u loaded=%u", blip.valid() ? 1 : 0, Resources::entryCount(), Resources::loadedCount());
    Resources::Unload(blip);
    Debug::Log("rensga: levels: platform=%s profile=%s memory=%u/%u", Application::platformName(Application::platform()),
               Application::profileName(Application::profile()), Budget::memoryUsed(), Budget::memoryLimit());
    SaveData d = {};
    const i32 n = SaveSystem::Read(0, kSaveVersion, &d, sizeof d, &Migrate);
    char line[64];
    if (n == static_cast<i32>(sizeof d) && d.level >= 1) {
      data = d;
      Debug::Log("rensga: levels: continue level=%d coins=%d lives=%d", data.level, data.coins, data.lives);
      snprintf(line, sizeof line, "CONTINUE LEVEL %d COINS %d", data.level, data.coins);
    } else {
      Debug::Log("rensga: levels: new game");
      snprintf(line, sizeof line, "NEW GAME");
    }
    SetText("MenuTitle", "RENSGA LEVELS");
    SetText("MenuHint", "SAVE SURVIVES A RESTART");
    SetText("MenuLine", line);
    snprintf(line, sizeof line, "RUN %d", runs);
    SetText("MenuRuns", line);
  }

  void BeginLoad(i32 level) {
    target = level;
    if (level > kLevels) {
      Debug::Log("rensga: levels: all levels done coins=%d", data.coins);
      Debug::Log("rensga: levels: %s", afterSave == 1 ? "idle" : "quit");
      if (afterSave != 1) Application::Quit();
      state = Finished;
      return;
    }
    SceneManager::LoadScene("Loading", LoadSceneMode::Additive);  // deferred to the end of this frame
    op = SceneManager::LoadSceneAsync(LevelName(level));
    op.setAllowSceneActivation(false);  // the level waits at 90% until the loading screen has been up long enough
    state = Loading;
    loadFrames = 0;
    Debug::Log("rensga: levels: loading level=%d start valid=%d", level, op.IsValid() ? 1 : 0);
  }

  void UpdateLoading() {
    ++loadFrames;
    const Scalar p = op.progress();
    const int pct = static_cast<int>(p / 0.9f * 100.0f + 0.5f);
    if (GameObject* fill = GameObject::Find("BarFill"))
      if (UIImage* img = fill->GetComponent<UIImage>()) img->w = static_cast<i32>(300.0f * (p / 0.9f));
    char b[32];
    snprintf(b, sizeof b, "%d PCT", pct > 100 ? 100 : pct);
    SetText("BarPct", b);
    snprintf(b, sizeof b, "LOADING LEVEL %d", target);
    SetText("LoadTitle", b);
    if (loadFrames % 10 == 1) Debug::Log("rensga: levels: loading level=%d progress=%d frames=%d done=%d", target, pct > 100 ? 100 : pct, loadFrames, op.isDone() ? 1 : 0);
    if (!op.isDone() && p >= 0.89f && loadFrames >= minLoadFrames && !op.allowSceneActivation()) {
      Debug::Log("rensga: levels: activating level=%d after %d frames", target, loadFrames);
      op.setAllowSceneActivation(true);
    }
    if (op.isDone()) {
      Debug::Log("rensga: levels: loaded level=%d scene=%s frames=%d", target, SceneManager::GetActiveScene().name(), loadFrames);
      state = Playing;
      playFrames = 0;
      hudStarted = false;
      levelDone = false;
    }
  }

  void UpdatePlaying() {
    ++playFrames;
    if (!hudStarted) {
      hudStarted = true;
      hudOp = SceneManager::LoadSceneAsync("Hud", LoadSceneMode::Additive);
      Debug::Log("rensga: levels: hud load started valid=%d", hudOp.IsValid() ? 1 : 0);
    }
    if (hudOp.IsValid() && hudOp.isDone() && playFrames % 8 == 0 && data.coins < target * 10) ++data.coins;  // the bot collects a coin
    char b[32];
    snprintf(b, sizeof b, "COINS %d", data.coins);
    SetText("HudCoins", b);
    snprintf(b, sizeof b, "LEVEL %d", target);
    SetText("HudLevel", b);
    SetText("LevelTitle", target == 1 ? "LEVEL 1 FOREST" : "LEVEL 2 CAVE");
    if (hudOp.IsValid() && hudOp.isDone() && playFrames == 12)
      Debug::Log("rensga: levels: hud loaded scenes=%u active=%s", SceneManager::loadedSceneCount(), SceneManager::GetActiveScene().name());
    if (!levelDone && data.coins >= target * 10) {
      levelDone = true;
      Debug::Log("rensga: levels: level complete level=%d coins=%d", target, data.coins);
      SaveData next = data;
      next.level = target + 1;
      SaveSystem::Metadata meta;
      snprintf(meta.title, sizeof meta.title, "RENSGA LEVELS");
      snprintf(meta.detail, sizeof meta.detail, "LEVEL %d COINS %d", next.level, next.coins);
      const bool ok = SaveSystem::Write(0, kSaveVersion, &next, sizeof next, &meta);
      SaveData back = {};
      const i32 n = SaveSystem::Read(0, kSaveVersion, &back, sizeof back, &Migrate);
      Debug::Log("rensga: levels: save written slot=0 version=%u level=%d coins=%d ok=%d", kSaveVersion, next.level, next.coins, ok ? 1 : 0);
      Debug::Log("rensga: levels: save readback ok=%d level=%d coins=%d", n == static_cast<i32>(sizeof back) ? 1 : 0, back.level, back.coins);
      PlayerPrefs::SetInt("lastLevel", target);
      PlayerPrefs::Save();
      data = next;
      if (afterSave == 2) {
        state = Menu;
        frames = 0;
        BeginLoad(target + 1);
      } else {
        Debug::Log("rensga: levels: budget memory=%u/%u vram=%u/%u voices=%u/%u", Budget::memoryUsed(), Budget::memoryLimit(), Budget::vramUsed(),
                   Budget::vramLimit(), Budget::voicesUsed(), Budget::voicesLimit());
        Debug::Log("rensga: levels: %s", afterSave == 0 ? "quit" : "idle");
        if (afterSave == 0) Application::Quit();
        state = Finished;
      }
    }
  }

  void Update() override {
    ++frames;
    switch (state) {
      case Menu:
        if (frames >= menuFrames) BeginLoad(data.level);
        break;
      case Loading: UpdateLoading(); break;
      case Playing: UpdatePlaying(); break;
      case Finished: break;
    }
  }
};
