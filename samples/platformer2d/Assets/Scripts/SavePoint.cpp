#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// A flag. The player touching it stores the best score and the coin count through ISaveStore (Save::SaveInt) and
// makes it the respawn position.
struct SavePoint : Behaviour {
  REFLECT(SavePoint)
  void OnTriggerEnter(GameObject* other) override {
    if (!other || !other->CompareTag("Player") || GameState::won) return;
    const Vector3 p = gameObject()->transform.position();
    GameState::checkpointX = p.x;
    GameState::checkpointY = p.y;
    ++GameState::saves;
    const bool ok = GameState::SaveGame();
    GameState::Sfx(GameState::kSfxSave);
    GameState::Message(ok ? "GAME SAVED  BEST " : "SAVE FAILED  BEST ", GameState::best);
    if (SpriteAnimator* an = gameObject()->GetComponent<SpriteAnimator>()) an->Play("flag_on");
  }
};
