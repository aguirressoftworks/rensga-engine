#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// The door at the end of a room (trigger box) and the goal flag of the last room. HudController runs the transition that follows.
struct RoomExit : Behaviour {
  REFLECT(RoomExit)
  TOOLTIP("The goal flag of the last room: reaching it finishes the game instead of loading the next room.")
  SERIALIZE_FIELD(bool, isGoal, false)

  void OnTriggerEnter(GameObject* other) override {
    if (!other || !other->CompareTag("Player") || GameState::Frozen()) return;
    if (isGoal) GameState::ReachGoal();
    else GameState::ReachExit();
    if (SpriteAnimator* an = gameObject()->GetComponent<SpriteAnimator>()) an->Play(isGoal ? "goal_on" : "door_open");
  }
};
