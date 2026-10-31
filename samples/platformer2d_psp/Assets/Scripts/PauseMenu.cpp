#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Pause on Start (the "Menu" action): the game time stops (Time::timeScale 0), the PauseUI object (panel, Resume, Quit to title)
// is shown and its buttons take the pad. Start again or Resume continues. Not available while a room transition runs.
struct PauseMenu : Behaviour {
  REFLECT(PauseMenu)
  Ref<GameObject> ui;
  bool shown = false;

  static void SetLabel(const char* objectName, const char* text) {
    GameObject* go = GameObject::Find(objectName);
    if (UIText* t = go ? go->GetComponent<UIText>() : nullptr) t->SetText(text);
  }
  void Start() override {
    SetLabel("PauseTitle", "PAUSED");
    SetLabel("ResumeLabel", "RESUME");
    SetLabel("QuitLabel", "QUIT TO TITLE");
    GameObject* go = GameObject::Find("PauseUI");
    if (go) ui = go->ref();
    GameState::paused = false;
    Show(false);
  }
  void OnDestroy() override {
    GameState::paused = false;
    Time::setTimeScale(1.0f);
  }
  void Show(bool on) {
    shown = on;
    if (GameObject* go = ui.get()) go->SetActive(on);
  }
  void Update() override {
    if (GameState::Frozen()) {
      if (GameState::paused) { GameState::paused = false; Time::setTimeScale(1.0f); }
    } else if (Input::GetButtonDown("Menu")) {
      GameState::paused = !GameState::paused;
      GameState::Sfx(GameState::kSfxSelect);
    }
    Time::setTimeScale(GameState::paused ? 0.0f : 1.0f);
    if (GameState::paused != shown) {
      Show(GameState::paused);
      UI::resetFocus();
    }
  }
};
