#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// A flag. The player touching it makes it the respawn position for the rest of the room (a lost life puts the player back here
// with full HP). The game itself is saved when a room is entered (GameState::BeginRoom), so Continue always starts a room fresh.
struct SavePoint : Behaviour {
  REFLECT(SavePoint)
  void OnTriggerEnter(GameObject* other) override {
    if (!other || !other->CompareTag("Player") || GameState::Frozen()) return;
    const Vector3 p = gameObject()->transform.position();
    GameState::checkpointX = p.x;
    GameState::checkpointY = p.y;
    ++GameState::saves;
    GameState::Sfx(GameState::kSfxSave);
    GameState::Message("CHECKPOINT");
    Debug::Log("rensga: checkpoint room=%d", static_cast<int>(GameState::room));
    if (SpriteAnimator* an = gameObject()->GetComponent<SpriteAnimator>()) an->Play("flag_on");
  }
};
