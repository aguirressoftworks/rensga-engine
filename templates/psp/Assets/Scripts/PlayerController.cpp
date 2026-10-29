#include <rensga/Rensga.h>

using namespace rensga;

// Camera-relative nub movement and jump. Movement runs in FixedUpdate so physics stays in step.
struct PlayerController : Behaviour {
  REFLECT(PlayerController)
  HEADER("Movement")
  RANGE(0, 20)
  SERIALIZE_FIELD(Scalar, speed, 6.0f)
  RANGE(0, 30)
  SERIALIZE_FIELD(Scalar, jumpSpeed, 8.0f)

  Vector2 stick;
  bool jumpQueued = false;

  void Update() override {
    stick = Input::GetAxis2("Move");
    if (Input::GetButtonDown("Jump")) jumpQueued = true;
  }

  void FixedUpdate() override {
    CharacterController* cc = gameObject()->GetComponent<CharacterController>();
    if (!cc) return;
    // Camera-relative basis on the ground plane.
    // Both axes come from the camera itself: the view looks down its local -Z with +X as screen right.
    Vector3 fwd(0, 0, 1), right(-1, 0, 0);
    World* w = gameObject()->world;
    Camera* cam = nullptr;
    w->forEach<Camera>([&](Camera& c) { if (!cam) cam = &c; });
    if (cam) {
      Vector3 look = cam->transform()->rotation() * Vector3(0, 0, -1);
      Vector3 side = cam->transform()->rotation() * Vector3(1, 0, 0);
      look.y = 0;
      side.y = 0;
      if (look.sqrMagnitude() > Mathf::Epsilon) fwd = look.normalized();
      if (side.sqrMagnitude() > Mathf::Epsilon) right = side.normalized();
    }
    Vector3 dir = right * stick.x + fwd * stick.y;
    if (dir.sqrMagnitude() > 1.0f) dir = dir.normalized();

    if (jumpQueued && cc->isGrounded()) cc->velocity.y = jumpSpeed;
    jumpQueued = false;

    cc->Move(dir * (speed * Time::fixedDeltaTime));
    if (dir.sqrMagnitude() > 0.01f)
      transform()->setRotation(Quaternion::AngleAxis(Mathf::Atan2(dir.x, dir.z) * Mathf::Rad2Deg, Vector3(0, 1, 0)));
  }
};
