#include <rensga/Rensga.h>

using namespace rensga;

// F6.02 level_walk: first-person walk through the gallery with the pad only (no mouse; the PSP has none).
//   nub up / down : walk forward / back          nub left / right : turn
//   L / R         : strafe                         D-pad up / down  : look up / down
// With autoWalk a fixed route (the waypoints below, forth and back) drives the same code, so an emulator without a pad can
// walk the level. Logs "rensga: walker pos=<x>,<z> yaw=<deg> wp=<i>" about once a second.
struct LevelWalker : Behaviour {
  REFLECT(LevelWalker)
  SERIALIZE_FIELD(bool, autoWalk, false)
  RANGE(0, 10)
  SERIALIZE_FIELD(Scalar, moveSpeed, 3.0f)
  RANGE(10, 360)
  SERIALIZE_FIELD(Scalar, turnSpeed, 100.0f)
  RANGE(0, 2)
  TOOLTIP("Eye height above the character controller's centre (its origin).")
  SERIALIZE_FIELD(Scalar, eyeHeight, 0.7f)

  Scalar yaw = 0, pitch = 0;
  Vector2 stick;
  Scalar strafe = 0, tilt = 0;
  i32 wp = 1, dir = 1;
  u32 frame = 0;
  static constexpr i32 kWaypoints = 7;

  static Vector3 waypoint(i32 i) {
    static const Scalar pts[kWaypoints][2] = {{0.0f, 8.0f}, {5.0f, 4.0f}, {5.0f, 0.0f}, {-5.0f, -1.5f}, {-5.0f, -6.0f}, {0.0f, -8.0f}, {6.0f, -8.0f}};
    return Vector3(pts[i][0], 0, pts[i][1]);
  }

  void Start() override {
    // the player faces the first waypoint
    const Vector3 p = transform()->position();
    const Vector3 t = waypoint(1);
    yaw = Mathf::Atan2(t.x - p.x, t.z - p.z) * Mathf::Rad2Deg;
    Debug::Log("rensga: walker ready auto=%d", autoWalk ? 1 : 0);
  }

  void Update() override {
    if (autoWalk) return;
    stick = Input::GetAxis2("Move");
    strafe = (Input::GetButton("StrafeRight") ? 1.0f : 0.0f) - (Input::GetButton("StrafeLeft") ? 1.0f : 0.0f);
    tilt = (Input::GetButton("LookUp") ? 1.0f : 0.0f) - (Input::GetButton("LookDown") ? 1.0f : 0.0f);
  }

  void FixedUpdate() override {
    CharacterController* cc = gameObject()->GetComponent<CharacterController>();
    if (!cc) return;
    const Scalar dt = Time::fixedDeltaTime();
    Vector3 pos = transform()->position();
    Scalar fwdIn = stick.y, rightIn = strafe;
    if (autoWalk) {
      const Vector3 t = waypoint(wp);
      Vector3 d = t - pos;
      d.y = 0;
      if (d.sqrMagnitude() < 0.25f) {
        wp += dir;
        if (wp >= kWaypoints) { dir = -1; wp = kWaypoints - 2; }
        if (wp < 0) { dir = 1; wp = 1; }
      }
      const Scalar want = Mathf::Atan2(d.x, d.z) * Mathf::Rad2Deg;
      Scalar diff = want - yaw;
      while (diff > 180.0f) diff -= 360.0f;
      while (diff < -180.0f) diff += 360.0f;
      const Scalar step = turnSpeed * dt;
      yaw += diff > step ? step : (diff < -step ? -step : diff);
      fwdIn = (diff > -35.0f && diff < 35.0f) ? 1.0f : 0.0f;
      rightIn = 0;
    } else {
      yaw += stick.x * turnSpeed * dt;
      pitch = Mathf::Clamp(pitch + tilt * 50.0f * dt, -50.0f, 50.0f);
    }
    const Scalar rad = yaw * Mathf::Deg2Rad;
    const Vector3 fwd(Mathf::Sin(rad), 0, Mathf::Cos(rad)), right(Mathf::Cos(rad), 0, -Mathf::Sin(rad));
    Vector3 move = fwd * fwdIn + right * rightIn;
    if (move.sqrMagnitude() > 1.0f) move = move.normalized();
    cc->Move(move * (moveSpeed * dt));
    transform()->setRotation(Quaternion::Euler(0, yaw, 0));
  }

  void LateUpdate() override {
    // the view looks down the camera's local -z, so the camera turns half a circle away from the player's forward
    if (GameObject* cam = GameObject::Find("Camera")) {
      cam->transform.setPosition(transform()->position() + Vector3(0, eyeHeight, 0));
      cam->transform.setRotation(Quaternion::Euler(-pitch, yaw + 180.0f, 0));
    }
    if (++frame % 60 == 0) {
      const Vector3 p = transform()->position();
      Debug::Log("rensga: walker pos=%d,%d yaw=%d wp=%d", static_cast<int>(p.x * 10), static_cast<int>(p.z * 10), static_cast<int>(yaw), wp);
    }
  }
};
