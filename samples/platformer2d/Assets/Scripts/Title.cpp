#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Title scene: fills the labels and shows the saved best score.
struct TitleScreen : Behaviour {
  REFLECT(TitleScreen)
  static void Label(const char* objectName, const char* text) {
    GameObject* go = GameObject::Find(objectName);
    UIText* t = go ? go->GetComponent<UIText>() : nullptr;
    if (t) t->SetText(text);
  }
  void Start() override {
    GameState::best = Save::LoadInt("best", 0);
    Label("TitleText", "PLATFORMER 2D");
    Label("StartLabel", "START");
    GameObject* hint = GameObject::Find("TitleHint");
    UIText* t = hint ? hint->GetComponent<UIText>() : nullptr;
    if (t) t->SetInt("BEST SCORE ", GameState::best);
  }
};

// The Start button: its UIButton press (focus + south button) loads the room.
struct TitleMenu : Behaviour {
  REFLECT(TitleMenu)
  void OnUIClick() override { SceneManager::LoadScene("Room"); }
};
