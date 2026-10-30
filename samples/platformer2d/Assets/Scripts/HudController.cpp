#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Coin counter, best score and the end-of-run message. Saves the best score through Save::SaveInt ("best").
struct HudController : Behaviour {
  REFLECT(HudController)
  RANGE(0, 600)
  SERIALIZE_FIELD(i32, returnDelayTicks, 90)

  Ref<UIText> coinsText, bestText, messageText;
  i32 ticksSinceWin = 0;

  static Ref<UIText> TextOf(const char* objectName) {
    GameObject* go = GameObject::Find(objectName);
    UIText* t = go ? go->GetComponent<UIText>() : nullptr;
    return t ? go->world->ref(t) : Ref<UIText>();
  }

  void Start() override {
    coinsText = TextOf("HudCoins");
    bestText = TextOf("HudBest");
    messageText = TextOf("HudMessage");
    GameState::best = Save::LoadInt("best", 0);
    ticksSinceWin = 0;
  }

  void Update() override {
    if (!GameState::won && GameState::coins >= GameState::kTotalCoins) {
      GameState::won = true;
      GameState::score = Mathf::Max(1, 6000 - GameState::fixedTicks);
      if (GameState::score > GameState::best) {
        GameState::best = GameState::score;
        Save::SaveInt("best", GameState::best);
        GameState::Sfx(GameState::kSfxSave);
      }
      Debug::Log("rensga: platformer won score=%d best=%d", static_cast<int>(GameState::score), static_cast<int>(GameState::best));
    }
    if (UIText* t = coinsText.get()) {
      t->SetInt("COINS ", GameState::coins);
    }
    if (UIText* t = bestText.get()) t->SetInt("BEST ", GameState::best);
    if (GameState::won) {
      if (UIText* t = messageText.get()) t->SetInt("ALL COINS!  SCORE ", GameState::score);
      if (++ticksSinceWin > returnDelayTicks && Input::GetButtonDown("Jump")) SceneManager::LoadScene("Title");
    }
  }
};
