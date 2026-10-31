#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// The HUD of a room (coins, lives, HP, game time, room number, a message line) and the room flow: it starts the room
// (GameState::BeginRoom saves the progress) and, after the player reached an exit, the goal or lost the last life, waits a moment and
// loads the next scene. Every text is anchored (UIText.anchor), so the same scene fits 640x448 and 480x272.
struct HudController : Behaviour {
  REFLECT(HudController)
  RANGE(1, 3)
  SERIALIZE_FIELD(i32, roomNumber, 1)
  RANGE(0, 600)
  SERIALIZE_FIELD(i32, exitDelayFrames, 70)
  RANGE(0, 600)
  SERIALIZE_FIELD(i32, endDelayFrames, 110)

  Ref<UIText> coinsText, livesText, hpText, timeText, roomText, messageText;
  i32 seenExit = 0;
  i32 framesInExit = 0;

  static Ref<UIText> TextOf(const char* objectName) {
    GameObject* go = GameObject::Find(objectName);
    UIText* t = go ? go->GetComponent<UIText>() : nullptr;
    return t ? go->world->ref(t) : Ref<UIText>();
  }

  void Start() override {
    coinsText = TextOf("HudCoins");
    livesText = TextOf("HudLives");
    hpText = TextOf("HudHp");
    timeText = TextOf("HudTime");
    roomText = TextOf("HudRoom");
    messageText = TextOf("HudMessage");
    GameState::BeginRoom(roomNumber);
    GameState::PlayMusic(GameState::kRoomTrack[(roomNumber < 1 ? 1 : (roomNumber > GameState::kRooms ? GameState::kRooms : roomNumber)) - 1]);
    seenExit = 0;
    framesInExit = 0;
  }

  void Update() override {
    if (GameState::exitRequest != seenExit) {
      seenExit = GameState::exitRequest;
      framesInExit = 0;
      if (seenExit == 1) GameState::Message("ROOM CLEARED");
      if (seenExit == 2) GameState::Message("YOU MADE IT");
    }
    if (seenExit != 0) {
      ++framesInExit;
      if (seenExit == 1 && framesInExit >= exitDelayFrames) SceneManager::LoadScene(GameState::RoomScene(GameState::room + 1));
      if (seenExit == 2 && framesInExit >= endDelayFrames) SceneManager::LoadScene("Results");
      if (seenExit == 3 && framesInExit >= endDelayFrames) SceneManager::LoadScene("GameOver");
    }
    char buf[UIText::kMaxText];
    if (UIText* t = coinsText.get()) t->SetInt("COINS ", GameState::coins);
    if (UIText* t = livesText.get()) t->SetInt("LIVES ", GameState::lives);
    if (UIText* t = hpText.get()) t->SetInt("HP ", GameState::hp);
    if (UIText* t = roomText.get()) t->SetInt("ROOM ", GameState::room);
    if (UIText* t = timeText.get()) {
      char clock[16];
      GameState::FormatTime(clock, sizeof clock, GameState::ticks);
      snprintf(buf, sizeof buf, "TIME %s", clock);
      t->SetText(buf);
    }
    if (UIText* msg = messageText.get()) {
      if (GameState::messageTicks > 0) {
        --GameState::messageTicks;
        msg->SetText(GameState::message);
      } else {
        msg->SetText("");
      }
    }
  }
};
