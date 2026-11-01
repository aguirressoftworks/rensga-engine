// F6.08 shooting range (samples/shooting_range): raycasts, layers, LayerMask, trigger zones and Rigidbody knock-back.
//
//   Layers (ProjectSettings/TagManager.json): 8 Target, 9 Ignore, 10 Zone, 11 Runner.
//   * Shots are Physics::Raycast calls with a LayerMask that leaves out Ignore (decoy panels that would block the line of sight) and Runner.
//     A second, unmasked ray from the same muzzle reports what the shot would have hit without the mask ("blocked by decoy").
//   * The Zone object is a trigger box on the Zone layer. ZoneCounter counts the Runner entering it (OnTriggerEnter / Stay / Exit).
//   * A hit Target (a Rigidbody) is pushed with AddForce(Impulse) and destroyed shortly after; the next shot goes to the next target.
//
// RangeGame.bot = true plays by itself (the AUTOSTART scene RangeBot): one shot every `shotInterval` fixed steps, every 4th shot is aimed
// beside its target. Without the bot the nub aims, South (Jump) fires. Log lines (grepped by tools/ci/physics_range.sh):
//   rensga: range ready targets=<n> mask=<hex>          rensga: range shot=<n> hit=<name> dist=<cm> ignored=<name|none>
//   rensga: range shot=<n> miss                          rensga: range zone enter=<n> by=<name>
//   rensga: range done shots=<n> hits=<n> blocked=<n> zone=<n>
#include <rensga/Rensga.h>

using namespace rensga;

constexpr u32 kLayerTarget = 8, kLayerIgnore = 9, kLayerZone = 10, kLayerRunner = 11;

struct ZoneCounter : Behaviour {
  REFLECT(ZoneCounter)
  i32 enters = 0, exits = 0, stays = 0;

  void OnTriggerEnter(GameObject* other) override {
    ++enters;
    Debug::Log("rensga: range zone enter=%d by=%s", static_cast<int>(enters), other ? other->name : "?");
  }
  void OnTriggerStay(GameObject*) override { ++stays; }
  void OnTriggerExit(GameObject*) override { ++exits; }
};

struct RangeGame : Behaviour {
  REFLECT(RangeGame)
  SERIALIZE_FIELD(bool, bot, false)
  RANGE(1, 600)
  SERIALIZE_FIELD(i32, shotInterval, 24)
  RANGE(1, 200)
  SERIALIZE_FIELD(i32, maxShots, 14)
  RANGE(0, 20)
  SERIALIZE_FIELD(Scalar, knockback, 6.0f)
  SERIALIZE_FIELD(i32, targetCount, 5)

  i32 tick = 0, shots = 0, hits = 0, blocked = 0, next = 0;
  bool done = false;
  Scalar yaw = 0, pitch = 0;
  InputAction* move = nullptr;
  Vector3 muzzle = Vector3(0, 1.6f, 9.0f);
  LayerMask shotMask = LayerMask(~((1u << kLayerIgnore) | (1u << kLayerRunner)));

  GameObject* targetAt(i32 i) const {
    char name[16];
    Format(name, sizeof name, "Target{0}", i);
    return GameObject::Find(name);
  }

  void Start() override {
    move = InputActions::Find("Move");
    Debug::Log("rensga: range ready targets=%d mask=%x bot=%d", static_cast<int>(targetCount), static_cast<unsigned>(shotMask.value), bot ? 1 : 0);
  }

  // Fires along `dir` from the muzzle. Returns true when a Target was hit.
  bool fire(const Vector3& dir) {
    ++shots;
    RaycastHit hit;
    const bool any = Physics::Raycast(muzzle, dir, &hit, 80.0f, shotMask, QueryTriggerInteraction::Ignore);  // the Zone trigger box sits on the line of fire
    RaycastHit all;
    const bool anyAll = Physics::Raycast(muzzle, dir, &all, 80.0f, LayerMask::All(), QueryTriggerInteraction::Ignore);
    const char* skipped = "none";
    if (anyAll && all.gameObject && (static_cast<u32>(all.gameObject->layer) & 31u) == kLayerIgnore) {
      skipped = all.gameObject->name;  // the decoy the masked ray went through
      ++blocked;
    }
    if (any && hit.gameObject && (static_cast<u32>(hit.gameObject->layer) & 31u) == kLayerTarget) {
      ++hits;
      Rigidbody* rb = hit.gameObject->GetComponent<Rigidbody>();
      if (rb) rb->AddForce(dir.normalized() * knockback, ForceMode::Impulse);
      Debug::Log("rensga: range shot=%d hit=%s dist=%d ignored=%s", static_cast<int>(shots), hit.gameObject->name, static_cast<int>(hit.distance * 100), skipped);
      Destroy(hit.gameObject, 0.7f);
      return true;
    }
    Debug::Log("rensga: range shot=%d miss", static_cast<int>(shots));
    return false;
  }

  void FixedUpdate() override {
    ++tick;
    // The Runner crosses the Zone trigger: pushed one way, then back.
    if (GameObject* runner = GameObject::Find("Runner")) {
      if (Rigidbody* rb = runner->GetComponent<Rigidbody>()) {
        if (tick % 150 == 1) rb->setVelocity(Vector3(((tick / 150) % 2) == 0 ? 5.0f : -5.0f, 0, 0));
      }
    }
    if (!bot) {
      Vector2 m = move ? move->ReadValue<Vector2>() : Vector2(0, 0);
      yaw += m.x * 40.0f * Time::fixedDeltaTime();
      pitch = Mathf::Clamp(pitch + m.y * 25.0f * Time::fixedDeltaTime(), -10.0f, 20.0f);
      yaw = Mathf::Clamp(yaw, -40.0f, 40.0f);
      if (Input::GetButtonDown("Jump")) {
        const Scalar ry = yaw * Mathf::Deg2Rad, rp = pitch * Mathf::Deg2Rad;
        fire(Vector3(Mathf::Sin(ry) * Mathf::Cos(rp), Mathf::Sin(rp), -Mathf::Cos(ry) * Mathf::Cos(rp)));
      }
      return;
    }
    if (done || tick % shotInterval != 0) return;
    if (shots >= maxShots) {
      done = true;
      ZoneCounter* z = zone();
      Debug::Log("rensga: range done shots=%d hits=%d blocked=%d zone=%d", static_cast<int>(shots), static_cast<int>(hits),
                 static_cast<int>(blocked), z ? static_cast<int>(z->enters) : -1);
      return;
    }
    // Next target that still stands (destroyed ones are skipped); every 4th shot is aimed 2 m to the side of it.
    GameObject* t = nullptr;
    for (i32 k = 0; k < targetCount && !t; ++k) {
      t = targetAt((next + k) % targetCount);
      if (t) next = (next + k + 1) % targetCount;
    }
    if (!t) { fireAt(Vector3(0, 1, -10)); return; }
    Vector3 aim = t->transform.position() + Vector3(0, 0.1f, 0);
    if ((shots + 1) % 4 == 0) aim.x += 2.0f;
    fireAt(aim);
  }

  void fireAt(const Vector3& aim) { fire((aim - muzzle).normalized()); }

  ZoneCounter* zone() const {
    GameObject* z = GameObject::Find("Zone");
    return z ? z->GetComponent<ZoneCounter>() : nullptr;
  }
};
