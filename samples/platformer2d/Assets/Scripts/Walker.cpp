#include <rensga/Rensga.h>

#include "EnemyContact.h"

using namespace rensga;

// Patrols left and right on the ground. Turns at walls, at ledges (no tile under the leading foot) and at its patrol limits.
struct Walker : Behaviour {
  REFLECT(Walker)
  RANGE(0, 300)
  SERIALIZE_FIELD(Scalar, speed, 60.0f)
  RANGE(0, 4000)
  SERIALIZE_FIELD(Scalar, gravity, 1400.0f)
  TOOLTIP("Patrol limits of the collider box, in world pixels. Both 0 = no limits.")
  SERIALIZE_FIELD(Scalar, patrolMinX, 0.0f)
  SERIALIZE_FIELD(Scalar, patrolMaxX, 0.0f)

  Scalar dir = 1.0f;
  Scalar vy = 0;
  bool dead = false;

  // Is there a tile (solid, one-way or slope) in the cell right under the foot at world x?
  static bool HasGround(const Tilemap* tm, Scalar x, Scalar footY) {
    if (!tm) return true;
    const Scalar ts = static_cast<Scalar>(tm->tileSize());
    const i32 cx = static_cast<i32>(Mathf::Floor(x / ts));
    const i32 cy = static_cast<i32>(Mathf::Floor((footY + 2.0f) / ts));
    return tm->TileFlags(tm->TileAt(cx, cy)) != 0;
  }

  void FixedUpdate() override {
    BoxCollider2D* box = gameObject()->GetComponent<BoxCollider2D>();
    if (!box || dead) return;
    const Scalar dt = Time::fixedDeltaTime();
    vy += gravity * dt;
    Move2DResult r = box->Move(dir * speed * dt, vy * dt);
    if (r.hitDown || r.hitUp) vy = 0;
    if ((dir < 0 && r.hitLeft) || (dir > 0 && r.hitRight)) dir = -dir;
    const Rect b = box->bounds();
    if (patrolMaxX > patrolMinX) {
      if (dir < 0 && b.x <= patrolMinX) dir = 1.0f;
      else if (dir > 0 && b.x + b.width >= patrolMaxX) dir = -1.0f;
    }
    if (box->grounded) {
      GameObject* map = GameObject::Find("Tilemap");
      const Tilemap* tm = map ? map->GetComponent<Tilemap>() : nullptr;
      const Scalar ahead = dir > 0 ? b.x + b.width + 1.0f : b.x - 1.0f;
      if (!HasGround(tm, ahead, b.y + b.height)) dir = -dir;
    }
    if (SpriteRenderer* sr = gameObject()->GetComponent<SpriteRenderer>()) sr->flipX = dir < 0;
    if (SpriteAnimator* an = gameObject()->GetComponent<SpriteAnimator>()) an->Play("enemy_walk");
    if (EnemyContact(gameObject(), box)) dead = true;
  }
};
