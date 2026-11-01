#include <rensga/Rensga.h>

#include "CoinGame.h"

using namespace rensga;

struct Coin : Behaviour {
  REFLECT(Coin)
  RANGE(0, 720)
  SERIALIZE_FIELD(Scalar, spin, 180.0f)
  SERIALIZE_FIELD(i32, value, 10)

  void Update() override { transform()->Rotate(Vector3(0, spin * Time::deltaTime(), 0)); }
  void OnTriggerEnter(GameObject* other) override {
    if (!other || !other->GetComponent<CharacterController>()) return;
    CoinScore::Collect(value);
    Destroy(gameObject());
  }
};
