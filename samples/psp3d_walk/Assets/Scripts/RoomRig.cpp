#include <rensga/Rensga.h>

using namespace rensga;

// F5 psp3d_walk: third-person camera and walking character, driven by the nub and the shoulder buttons only.
//   nub        : the character walks, relative to where the camera looks (up = away from the camera)
//   L / R      : the camera orbits the character
//   D-pad up/down : camera pitch
// With autoWalk the same code is driven by a fixed script instead of the pad (an emulator has no pad): the
// character walks a circle and the camera follows. Without a "Character" object (the Lights12 scene) the camera
// orbits the middle of the scene.
// Logs "rensga: rig player=<x>,<z> yaw=<deg> walking=<0|1>" about once a second while something changes.
struct RoomRig : Behaviour {
  REFLECT(RoomRig)
  SERIALIZE_FIELD(bool, autoWalk, false)
  RANGE(2, 20)
  SERIALIZE_FIELD(Scalar, distance, 5.0f)
  RANGE(10, 60)
  SERIALIZE_FIELD(Scalar, pitchStart, 22.0f)
  RANGE(0, 360)
  SERIALIZE_FIELD(Scalar, orbitSpeed, 90.0f)
  RANGE(0, 10)
  SERIALIZE_FIELD(Scalar, moveSpeed, 2.2f)
  RANGE(0, 12)
  SERIALIZE_FIELD(Scalar, circleRadius, 2.5f)
  // Test aid: at this script frame RenderSettings.fogEnabled is switched off (0 = never), so a screenshot test can compare with and without fog.
  SERIALIZE_FIELD(i32, fogToggleFrame, 0)

  Scalar yaw = 0, pitch = 0, angle = 0;
  InputAction* move = nullptr;
  Ref<GameObject> player;
  Animation* anim = nullptr;
  u32 frame = 0;
  bool walking = false;

  void Start() override {
    move = InputActions::Find("Move");
    pitch = pitchStart;
    if (GameObject* p = GameObject::Find("Character")) {
      player = p->ref();
      anim = p->GetComponent<Animation>();
    }
    Debug::Log("rensga: rig ready player=%d auto=%d", player ? 1 : 0, autoWalk ? 1 : 0);
  }

  void LateUpdate() override {
    const Scalar dt = Time::deltaTime();
    Scalar orbit = 0, tilt = 0;
    Vector2 m(0, 0);
    if (!autoWalk) {
      orbit = (Input::GetButton("CameraLeft") ? 1.0f : 0.0f) - (Input::GetButton("CameraRight") ? 1.0f : 0.0f);
      tilt = (Input::GetButton("CameraUp") ? 1.0f : 0.0f) - (Input::GetButton("CameraDown") ? 1.0f : 0.0f);
      if (move) m = move->ReadValue<Vector2>();
    } else {
      orbit = player ? 0.0f : 0.25f;
    }
    yaw += orbit * orbitSpeed * dt;
    pitch = Mathf::Clamp(pitch + tilt * 40.0f * dt, 5.0f, 60.0f);

    Vector3 target(0, 0.5f, 0);
    if (player) {
      Vector3 pos = player->transform.position();
      Vector3 step(0, 0, 0);
      const Scalar rad = yaw * Mathf::Deg2Rad;
      if (autoWalk) {
        // a circle around (0,-1): the character walks at moveSpeed, so the angular speed is moveSpeed / radius
        angle += (moveSpeed / circleRadius) * dt;
        const Vector3 next(Mathf::Sin(angle) * circleRadius, pos.y, -1.0f + Mathf::Cos(angle) * circleRadius);
        step = next - pos;
        pos = next;
      } else {
        const Vector3 fwd(Mathf::Sin(rad), 0, Mathf::Cos(rad));
        const Vector3 right(Mathf::Cos(rad), 0, -Mathf::Sin(rad));
        step = (fwd * m.y + right * m.x) * (moveSpeed * dt);
        pos = pos + step;
      }
      const bool moving = step.x * step.x + step.z * step.z > 1e-8f;
      if (moving) {
        // the model faces +z
        player->transform.setRotation(Quaternion::Euler(0, Mathf::Atan2(step.x, step.z) * 57.29578f, 0));
      }
      player->transform.setPosition(pos);
      if (anim) {
        if (moving && !anim->isPlaying()) anim->Play();
        if (!moving && anim->isPlaying()) anim->Stop();
      }
      walking = moving;
      target = pos + Vector3(0, 1.0f, 0);
    }

    const Scalar rad = yaw * Mathf::Deg2Rad, prad = pitch * Mathf::Deg2Rad;
    const Scalar d = player ? distance : 12.0f;
    const Vector3 off(-Mathf::Sin(rad) * Mathf::Cos(prad) * d, Mathf::Sin(prad) * d, -Mathf::Cos(rad) * Mathf::Cos(prad) * d);
    transform()->setPosition(target + off);
    // the view looks down the camera's local -z, so turn it away from the offset direction (+180)
    transform()->setRotation(Quaternion::Euler(-pitch, yaw + 180.0f, 0));

    if (fogToggleFrame > 0 && static_cast<i32>(frame) + 1 == fogToggleFrame) {
      GameObject* g = GameObject::Find("RenderSettings");
      RenderSettings* r = g ? g->GetComponent<RenderSettings>() : nullptr;
      if (r) r->fogEnabled = false;
      Debug::Log("rensga: rig fog off at frame %d", fogToggleFrame);
    }
    if (++frame % 60 == 0) {
      const Vector3 p = player ? player->transform.position() : target;
      Debug::Log("rensga: rig player=%d,%d yaw=%d walking=%d", static_cast<int>(p.x * 10), static_cast<int>(p.z * 10),
                 static_cast<int>(yaw), walking ? 1 : 0);
    }
  }
};
