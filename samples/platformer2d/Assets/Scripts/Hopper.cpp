#include <rensga/Rensga.h>

#include "EnemyContact.h"

using namespace rensga;

// Sits, then hops forward every hopTicks fixed steps. Turns at walls and its patrol limits.
struct Hopper : Behaviour {
  REFLECT(Hopper)
  RANGE(1, 600)
  SERIALIZE_FIELD(i32, hopTicks, 50)
  RANGE(0, 1200)
  SERIALIZE_FIELD(Scalar, hopSpeed, 380.0f)
  RANGE(0, 400)
  SERIALIZE_FIELD(Scalar, moveSpeed, 90.0f)
  RANGE(0, 4000)
  SERIALIZE_FIELD(Scalar, gravity, 1400.0f)
  TOOLTIP("Patrol limits of the collider box, in world pixels. Both 0 = no limits.")
  SERIALIZE_FIELD(Scalar, patrolMinX, 0.0f)
  SERIALIZE_FIELD(Scalar, patrolMaxX, 0.0f)

  Scalar dir = -1.0f;
  Scalar vy = 0;
  Scalar vx = 0;
  i32 timer = 0;
  bool dead = false;

  void FixedUpdate() override {
    BoxCollider2D* box = gameObject()->GetComponent<BoxCollider2D>();
    if (!box || dead) return;
    const Scalar dt = Time::fixedDeltaTime();
    if (box->grounded) {
      vx = 0;
      if (++timer >= hopTicks) {
        timer = 0;
        vy = -hopSpeed;
        vx = dir * moveSpeed;
      }
    }
    vy += gravity * dt;
    Move2DResult r = box->Move(vx * dt, vy * dt);
    if (r.hitDown || r.hitUp) vy = 0;
    if ((dir < 0 && r.hitLeft) || (dir > 0 && r.hitRight)) { dir = -dir; vx = -vx; }
    const Rect b = box->bounds();
    if (patrolMaxX > patrolMinX) {
      if (dir < 0 && b.x <= patrolMinX) { dir = 1.0f; if (vx < 0) vx = -vx; }
      else if (dir > 0 && b.x + b.width >= patrolMaxX) { dir = -1.0f; if (vx > 0) vx = -vx; }
    }
    if (SpriteRenderer* sr = gameObject()->GetComponent<SpriteRenderer>()) sr->flipX = dir < 0;
    if (SpriteAnimator* an = gameObject()->GetComponent<SpriteAnimator>()) an->Play(box->grounded ? "hopper_idle" : "hopper_air");
    if (EnemyContact(gameObject(), box)) dead = true;
  }
};
