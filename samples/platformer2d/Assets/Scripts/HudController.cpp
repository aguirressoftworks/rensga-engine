#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Coins, HP, best score, a message line, and the end of the room: all coins -> "ROOM CLEARED".
// The best score is stored through Save::SaveInt ("best", "coins"), also by the save point.
struct HudController : Behaviour {
  REFLECT(HudController)
  RANGE(0, 600)
  SERIALIZE_FIELD(i32, returnDelayTicks, 90)
  RANGE(0, 6000)
  SERIALIZE_FIELD(i32, timeBonusTicks, 3600)

  Ref<UIText> coinsText, hpText, bestText, messageText;
  i32 ticksSinceWin = 0;

  static Ref<UIText> TextOf(const char* objectName) {
    GameObject* go = GameObject::Find(objectName);
    UIText* t = go ? go->GetComponent<UIText>() : nullptr;
    return t ? go->world->ref(t) : Ref<UIText>();
  }

  void Start() override {
    coinsText = TextOf("HudCoins");
    hpText = TextOf("HudHp");
    bestText = TextOf("HudBest");
    messageText = TextOf("HudMessage");
    GameState::best = Save::LoadInt("best", 0);
    ticksSinceWin = 0;
  }

  void Update() override {
    if (!GameState::won && GameState::coins >= GameState::kTotalCoins) {
      GameState::won = true;
      const i32 bonus = Mathf::Max(0, (timeBonusTicks - GameState::fixedTicks) / 4);
      GameState::score = GameState::RunScore() + bonus;
      if (GameState::score > GameState::best) GameState::best = GameState::score;
      GameState::Sfx(GameState::kSfxSave);
      Debug::Log("rensga: room cleared coins=%d score=%d", static_cast<int>(GameState::coins), static_cast<int>(GameState::score));
      const bool ok = Save::SaveInt("best", GameState::best) && Save::SaveInt("coins", GameState::coins);
      Debug::Log("rensga: save readback best=%d coins=%d ok=%d", static_cast<int>(Save::LoadInt("best", -1)),
                 static_cast<int>(Save::LoadInt("coins", -1)), ok ? 1 : 0);
    }
    if (UIText* t = coinsText.get()) t->SetInt("COINS ", GameState::coins);
    if (UIText* t = hpText.get()) t->SetInt("HP ", GameState::hp);
    if (UIText* t = bestText.get()) t->SetInt("BEST ", GameState::best);
    UIText* msg = messageText.get();
    if (GameState::won) {
      if (msg) msg->SetInt("ROOM CLEARED  SCORE ", GameState::score);
      if (++ticksSinceWin > returnDelayTicks && Input::GetButtonDown("Jump")) SceneManager::LoadScene("Title");
    } else if (msg) {
      if (GameState::messageTicks > 0) {
        --GameState::messageTicks;
        msg->SetText(GameState::message);
      } else {
        msg->SetText("");
      }
    }
  }
};
