// Side contact hurts the player (knockback, one HP); a stomp from above defeats the enemy and bounces the player.
#pragma once
#include "GameState.h"

// Returns true on the step the enemy is stomped. `self` is destroyed (deferred to the end of the frame).
inline bool EnemyContact(rensga::GameObject* self, rensga::BoxCollider2D* enemyBox) {
  using namespace rensga;
  if (GameState::won) return false;
  GameObject* p = GameObject::Find("Player");
  BoxCollider2D* pb = p ? p->GetComponent<BoxCollider2D>() : nullptr;
  if (!pb || !pb->Overlaps(*enemyBox)) return false;
  const Rect a = pb->bounds(), e = enemyBox->bounds();
  if (GameState::playerVy > 0.0f && a.y + a.height <= e.y + 14.0f) {
    ++GameState::stomps;
    GameState::bounceRequest = true;
    GameState::Sfx(GameState::kSfxHit);
    GameState::Message("STOMP  SCORE ", GameState::RunScore());
    Debug::Log("rensga: stomp %s stomps=%d", self->name, static_cast<int>(GameState::stomps));
    Destroy(self);
    return true;
  }
  if (GameState::hurtTicks == 0 && GameState::hp > 0 && GameState::hurtRequest == 0) {
    GameState::hurtRequest = (a.x + a.width * 0.5f < e.x + e.width * 0.5f) ? -1 : 1;
  }
  return false;
}
