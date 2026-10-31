#include "rensga/core/rigidbody2d.h"

#include "rensga/core/time.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {
Scalar g_gravity = Rigidbody2DSim::kDefaultGravity;
}

Scalar Rigidbody2DSim::gravity() { return g_gravity; }
void Rigidbody2DSim::setGravity(Scalar g) { g_gravity = g; }

void Rigidbody2DSim::fixedStep(World& world) {
  const Scalar dt = Time::fixedDeltaTime();
  if (!(dt > 0)) return;
  world.forEach<Rigidbody2D>([&](Rigidbody2D& rb) {
    if (!rb.enabled() || rb.bodyType == RigidbodyType::Static) return;
    GameObject* go = rb.gameObject();
    if (!go || !go->activeInHierarchy()) return;
    BoxCollider2D* box = go->GetComponent<BoxCollider2D>();
    if (!box || !box->enabled()) return;

    const bool dynamic = rb.bodyType == RigidbodyType::Dynamic;
    if (dynamic) rb.velocityY += g_gravity * rb.gravityScale * dt;

    const bool autoDrop = box->autoDropThrough;
    box->autoDropThrough = false;
    const Move2DResult hit = box->Move(rb.velocityX * dt, rb.velocityY * dt);
    box->autoDropThrough = autoDrop;

    if (!dynamic) return;
    if (hit.hitDown || (box->grounded && rb.velocityY > 0)) rb.velocityY = 0;
    if (hit.hitUp && rb.velocityY < 0) rb.velocityY = 0;
    if (hit.hitLeft || hit.hitRight) rb.velocityX = 0;
  });
}

}  // namespace rensga
