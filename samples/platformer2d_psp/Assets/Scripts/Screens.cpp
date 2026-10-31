#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

namespace {
void Label(const char* objectName, const char* text) {
  GameObject* go = GameObject::Find(objectName);
  UIText* t = go ? go->GetComponent<UIText>() : nullptr;
  if (t) t->SetText(text);
}
void Label(const char* objectName, const char* prefix, i32 value) {
  GameObject* go = GameObject::Find(objectName);
  UIText* t = go ? go->GetComponent<UIText>() : nullptr;
  if (t) t->SetInt(prefix, value);
}
void TimeLabel(const char* objectName, const char* prefix, i32 ticks) {
  char clock[16], buf[UIText::kMaxText];
  GameState::FormatTime(clock, sizeof clock, ticks);
  snprintf(buf, sizeof buf, "%s%s", prefix, clock);
  Label(objectName, buf);
}
}  // namespace

// Title scene: texts, the best time, and the Continue button (greyed out and skipped by the focus while there is no saved game).
struct TitleScreen : Behaviour {
  REFLECT(TitleScreen)
  void Start() override {
    GameState::LoadBest();
    GameState::PlayMusic("music.wav");
    GameState::paused = false;
    Time::setTimeScale(1.0f);
    Label("TitleText", "PLATFORMER 2D");
    Label("NewLabel", "NEW GAME");
    const bool saved = GameState::HasSave();
    GameObject* cont = GameObject::Find("ContinueButton");
    if (UIButton* b = cont ? cont->GetComponent<UIButton>() : nullptr) b->interactable = saved;
    if (saved) Label("ContinueLabel", "CONTINUE  ROOM ", Save::LoadInt("room", 1));
    else Label("ContinueLabel", "CONTINUE");
    if (GameState::bestTicks > 0) TimeLabel("TitleBest", "BEST TIME ", GameState::bestTicks);
    else Label("TitleBest", "BEST TIME --");
    Label("TitleHint", "D-PAD MOVE   SOUTH JUMP   START PAUSE");
  }
};

// Game over scene: "Continue" restarts the room from its entry with full lives, the other button goes to the title.
struct GameOverScreen : Behaviour {
  REFLECT(GameOverScreen)
  void Start() override {
    Time::setTimeScale(1.0f);
    GameState::StopMusic();
    Label("OverText", "GAME OVER");
    Label("OverCoins", "COINS ", GameState::coins);
    TimeLabel("OverTime", "TIME ", GameState::ticks);
    Label("OverRoom", "ROOM ", GameState::room);
    Label("RetryLabel", "CONTINUE");
    Label("QuitLabel", "QUIT TO TITLE");
  }
};

// Results scene: time (game time), coins and the best time of the saved game.
struct ResultsScreen : Behaviour {
  REFLECT(ResultsScreen)
  void Start() override {
    Time::setTimeScale(1.0f);
    GameState::PlayMusic("music.wav");
    Label("ResText", "GAME COMPLETE");
    TimeLabel("ResTime", "TIME ", GameState::finalTicks);
    char buf[UIText::kMaxText];
    snprintf(buf, sizeof buf, "COINS %d/%d", static_cast<int>(GameState::finalCoins), static_cast<int>(GameState::kTotalCoins));
    Label("ResCoins", buf);
    if (GameState::newRecord) Label("ResBest", "NEW BEST TIME");
    else TimeLabel("ResBest", "BEST TIME ", GameState::bestTicks);
    Label("AgainLabel", "PLAY AGAIN");
    Label("MenuLabel", "TITLE");
  }
};
