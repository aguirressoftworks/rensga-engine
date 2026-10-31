#include <rensga/Rensga.h>

using namespace rensga;

// F5.13 InputCam: a third-person camera and a player that need only the nub and the shoulder buttons.
//   nub        : the player walks, relative to where the camera looks (up = away from the camera)
//   L / R      : the camera orbits the player (face buttons: Square / Circle do the same, for a pad without shoulders)
//   D-pad up/down : camera pitch
// The actions come from ProjectSettings/InputActions.json (Move, CameraLeft, CameraRight, CameraUp, CameraDown).
// Logs "rensga: cam yaw=<deg> pitch=<deg> player=<x>,<z>" when it changes (at most every 10 frames) and OnApplicationPause.
struct CamRig : Behaviour {
  REFLECT(CamRig)
  RANGE(2, 20)
  SERIALIZE_FIELD(Scalar, distance, 7.0f)
  RANGE(0, 360)
  SERIALIZE_FIELD(Scalar, orbitSpeed, 100.0f)
  RANGE(0, 20)
  SERIALIZE_FIELD(Scalar, moveSpeed, 4.0f)

  Scalar yaw = 0, pitch = 25;
  InputAction* move = nullptr;
  Ref<GameObject> player;
  u32 frame = 0;
  i32 lastYaw = -9999, lastPitch = -9999, lastX = -9999, lastZ = -9999;

  void Start() override {
    move = InputActions::Find("Move");
    GameObject* p = GameObject::Find("Player");
    if (p) player = p->ref();
    Debug::Log("rensga: cam ready player=%d", player ? 1 : 0);
  }

  void OnApplicationPause(bool paused) override { Debug::Log("rensga: OnApplicationPause(%s)", paused ? "true" : "false"); }

  void LateUpdate() override {
    if (!player || !move) return;
    const Scalar dt = Time::deltaTime();
    // L turns the view left (the camera goes right), R the other way, like templates/psp-3d FollowCamera.
    yaw += ((Input::GetButton("CameraLeft") ? 1.0f : 0.0f) - (Input::GetButton("CameraRight") ? 1.0f : 0.0f)) * orbitSpeed * dt;
    pitch += ((Input::GetButton("CameraUp") ? 1.0f : 0.0f) - (Input::GetButton("CameraDown") ? 1.0f : 0.0f)) * 40.0f * dt;
    pitch = Mathf::Clamp(pitch, 5.0f, 60.0f);

    const Scalar rad = yaw * Mathf::Deg2Rad;
    const Vector3 fwd(Mathf::Sin(rad), 0, Mathf::Cos(rad));
    const Vector3 right(Mathf::Cos(rad), 0, -Mathf::Sin(rad));
    const Vector2 m = move->ReadValue<Vector2>();
    Vector3 pos = player->transform.position();
    pos = pos + (fwd * m.y + right * m.x) * (moveSpeed * dt);
    player->transform.setPosition(pos);

    const Scalar prad = pitch * Mathf::Deg2Rad;
    const Vector3 off(-Mathf::Sin(rad) * Mathf::Cos(prad) * distance, Mathf::Sin(prad) * distance, -Mathf::Cos(rad) * Mathf::Cos(prad) * distance);
    transform()->setPosition(pos + off);
    // The view looks down the camera's local -Z, so face away from the player direction (+180), like templates/psp-3d FollowCamera.
    transform()->setRotation(Quaternion::Euler(-pitch, yaw + 180.0f, 0));

    if (++frame % 10 == 0) {
      const i32 y = static_cast<i32>(yaw), p = static_cast<i32>(pitch), x = static_cast<i32>(pos.x * 10), z = static_cast<i32>(pos.z * 10);
      if (y != lastYaw || p != lastPitch || x != lastX || z != lastZ) {
        lastYaw = y; lastPitch = p; lastX = x; lastZ = z;
        Debug::Log("rensga: cam yaw=%d pitch=%d player=%d,%d (tenths)", y, p, x, z);
        if (GameObject* o = GameObject::Find("CamLine")) {
          if (UIText* t = o->GetComponent<UIText>()) t->SetInt("YAW ", y);
        }
      }
    }
  }
};
