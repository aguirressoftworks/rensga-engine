#include <rensga/Rensga.h>

#include "PickupCounter.h"

using namespace rensga;

struct Pickup : Behaviour {
  REFLECT(Pickup)
  RANGE(0, 360)
  SERIALIZE_FIELD(Scalar, spinSpeed, 90.0f)

  void Update() override { transform()->Rotate(Vector3(0, spinSpeed * Time::deltaTime, 0)); }
  void OnTriggerEnter(GameObject* other) override {
    if (!other || !other->GetComponent<CharacterController>()) return;
    PickupCounter::Collect();
    Destroy(gameObject());
  }
};
