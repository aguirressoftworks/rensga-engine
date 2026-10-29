#include <rensga/Rensga.h>

using namespace rensga;

// Follows the target and orbits around it with the L and R shoulder buttons.
struct FollowCamera : Behaviour {
  REFLECT(FollowCamera)
  SERIALIZE_FIELD(Guid, target, Guid())
  RANGE(1, 50)
  SERIALIZE_FIELD(Scalar, distance, 8.0f)
  RANGE(0, 30)
  SERIALIZE_FIELD(Scalar, height, 4.0f)
  RANGE(0, 360)
  SERIALIZE_FIELD(Scalar, orbitSpeed, 90.0f)

  Scalar yaw = 0;  // degrees
  Ref<GameObject> targetRef;

  void LateUpdate() override {
    if (!targetRef) {
      GameObject* t = gameObject()->world->findByGuid(target);
      if (!t) return;
      targetRef = t->ref();
    }
    // R turns the view to the right; with the camera behind the target that means decreasing yaw.
    yaw += ((Input::GetButton("CameraLeft") ? 1.0f : 0.0f) - (Input::GetButton("CameraRight") ? 1.0f : 0.0f)) *
           orbitSpeed * Time::deltaTime;
    Vector3 focus = targetRef->transform.position();
    Scalar rad = yaw * Mathf::Deg2Rad;
    transform()->setPosition(focus + Vector3(-Mathf::Sin(rad) * distance, height, -Mathf::Cos(rad) * distance));
    // The view looks down the camera's local -Z, so face away from the target direction (+180).
    Scalar pitch = Mathf::Atan2(height, distance) * Mathf::Rad2Deg;
    transform()->setRotation(Quaternion::Euler(-pitch, yaw + 180.0f, 0));
  }
};
