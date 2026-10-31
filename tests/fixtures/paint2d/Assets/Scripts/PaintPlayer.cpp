#include <rensga/Rensga.h>

using namespace rensga;

// Fixture script of exercise_paint_level (F4.08): walks the object's Rigidbody2D with the pad. Gravity, the floor and the
// walls are the Rigidbody2D's job (Rigidbody2D + BoxCollider2D + the painted TilemapCollider2D).
struct PaintPlayer : Behaviour {
  REFLECT(PaintPlayer)
  RANGE(0, 600)
  SERIALIZE_FIELD(Scalar, moveSpeed, 120.0f)

  void FixedUpdate() override {
    Rigidbody2D* body = gameObject()->GetComponent<Rigidbody2D>();
    if (!body) return;
    body->velocityX = Input::GetAxis2("Pad").x * moveSpeed;
  }
};
