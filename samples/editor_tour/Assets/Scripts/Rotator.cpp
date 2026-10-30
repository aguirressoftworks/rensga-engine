#include <rensga/Rensga.h>

using namespace rensga;

// Editor 4 exercise "hot-reload a Rotator": turns its object around Y. The speed is an exposed field, so it can be changed in
// the inspector while the scene plays. The state lives in the transform, so it survives a script hot reload.
struct Rotator : Behaviour {
  REFLECT(Rotator)
  TOOLTIP("Degrees per second")
  RANGE(-720, 720)
  SERIALIZE_FIELD(Scalar, speed, 45.0f)

  void Update() override {
    transform()->setRotation(transform()->rotation() * Quaternion::Euler(0, speed * Time::deltaTime(), 0));
  }
};
