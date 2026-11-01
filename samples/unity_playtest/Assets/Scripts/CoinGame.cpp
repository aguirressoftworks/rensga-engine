// Collect the coins before the time runs out. Everything tunable is in the GameTuning data asset; the coin is a prefab.
//
// Log lines (grepped by tools/ci/unity_playtest.sh):
//   rensga: coingame start coins=<n> limit_s=<n>        rensga: coin collected <n>/<m> score=<s>
//   rensga: coingame done score=<s> collected=<n>/<m> best=<b> reason=<all|time>
#include <rensga/Rensga.h>

#include "CoinGame.h"
#include "GameTuning.h"

using namespace rensga;

struct CoinGame : Behaviour {
  REFLECT(CoinGame)
  SERIALIZE_FIELD(Guid, tuning, Guid())
  SERIALIZE_FIELD(Guid, coinPrefab, Guid())
  TOOLTIP("Quit when the round ends (the bot scene on an emulator)")
  SERIALIZE_FIELD(bool, quitWhenDone, false)

  const GameTuning* t = nullptr;
  UIText* label = nullptr;
  Scalar timeLeft = 30.0f;
  bool done = false;
  u32 seed = 2024;

  Scalar Rand01() {  // the same coins on every platform
    seed = seed * 1664525u + 1013904223u;
    return static_cast<Scalar>((seed >> 8) & 0xFFFF) / 65535.0f;
  }

  void Start() override {
    CoinScore::score = CoinScore::collected = CoinScore::spawned = 0;
    t = DataAssets::Get<GameTuning>(tuning);
    if (!t) { Debug::Log("rensga: coingame no tuning asset"); return; }
    timeLeft = t->timeLimit;
    if (coinPrefab.isNull()) { Debug::Log("rensga: coingame no coin prefab"); return; }
    for (i32 i = 0; i < t->coinCount; ++i) {
      const Vector3 p((Rand01() * 2 - 1) * t->arenaHalf, 0.6f, (Rand01() * 2 - 1) * t->arenaHalf);
      if (Instantiate(coinPrefab, p, Quaternion())) ++CoinScore::spawned;
    }
    GameObject* go = GameObject::Find("ScoreText");
    label = go ? go->GetComponent<UIText>() : nullptr;
    Debug::Log("rensga: coingame start coins=%d limit_s=%d", static_cast<int>(CoinScore::spawned), static_cast<int>(t->timeLimit));
  }

  void Update() override {
    if (!t || done) return;
    timeLeft -= Time::deltaTime();
    if (label) label->SetFormat("Score {0}   Coins {1}/{2}   Time {3}", CoinScore::score, CoinScore::collected, CoinScore::spawned, static_cast<i32>(timeLeft > 0 ? timeLeft : 0));
    const bool all = CoinScore::collected >= CoinScore::spawned;
    if (all || timeLeft <= 0) {
      done = true;
      const i32 best = PlayerPrefs::GetInt("best", 0);
      if (CoinScore::score > best) { PlayerPrefs::SetInt("best", CoinScore::score); PlayerPrefs::Save(); }
      Debug::Log("rensga: coingame done score=%d collected=%d/%d best=%d reason=%s", static_cast<int>(CoinScore::score), static_cast<int>(CoinScore::collected),
                 static_cast<int>(CoinScore::spawned), static_cast<int>(PlayerPrefs::GetInt("best", 0)), all ? "all" : "time");
      if (quitWhenDone) Application::Quit();
    }
  }
};
