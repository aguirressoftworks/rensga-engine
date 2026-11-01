// The tuning of the game, as a data asset (Unity: a ScriptableObject). Edit Assets/Data/Tuning.asset.json in the Inspector.
#pragma once
#include <rensga/Rensga.h>

using namespace rensga;

struct GameTuning : DataAsset {
  REFLECT(GameTuning)
  HEADER("Player")
  RANGE(1, 12)
  SERIALIZE_FIELD(Scalar, playerSpeed, 5.0f)
  HEADER("Coins")
  RANGE(1, 16)
  SERIALIZE_FIELD(i32, coinCount, 8)
  SERIALIZE_FIELD(i32, coinValue, 10)
  HEADER("Round")
  RANGE(5, 120)
  SERIALIZE_FIELD(Scalar, timeLimit, 30.0f)
  RANGE(2, 7)
  SERIALIZE_FIELD(Scalar, arenaHalf, 6.0f)
};
