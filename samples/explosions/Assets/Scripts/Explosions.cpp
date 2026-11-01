// F6.09 explosions (samples/explosions): the rendering API of API 7 in one small scene.
//
//   * Camera: the pointer (mouse click on the desktop, the nub and South on the PSP, fixed screen points for the bot) becomes a world
//     point with Camera::ScreenPointToRay and the ground plane; WorldToScreenPoint gives the screen point back (logged as back=).
//     A second, orthographic camera draws a minimap into the top-right viewport (depth 1, its own clear, a culling mask that leaves out
//     the Tower on layer 8).
//   * ParticleSystem: every explosion is three systems (Fire, Smoke, Sparks) from a pool of `booms` sets, emitted with Emit().
//     The storm at the end of the bot run emits far more than maxParticles so the pools sit at their capacity; allocs stay 0.
//   * MaterialPropertyBlock: the crates share one material; the block tints each one individually by its distance to the blast.
//   * LineRenderer: the Marker ring follows the last blast. Debug::DrawLine draws a beam in dev builds.
//   * Camera shake: the camera is moved in LateUpdate by a decaying pseudo-random offset (a private generator, so the run is repeatable).
//
// Log lines (grepped by tools/ci/explosions.sh):
//   rensga: fx ready booms=<n> crates=<n> pointer=<mouse|nub|bot>
//   rensga: fx boom=<n> screen=<x>,<y> world=<cm x>,<cm z> back=<x>,<y> live=<n>
//   rensga: fx done booms=<n> live=<n> reserved=<n> rejected=<n>
#include <rensga/Rensga.h>
#include <rensga/core/render_view.h>

using namespace rensga;

struct ExplosionGame : Behaviour {
  REFLECT(ExplosionGame)
  SERIALIZE_FIELD(bool, bot, false)
  RANGE(1, 600)
  SERIALIZE_FIELD(i32, interval, 36)
  RANGE(1, 64)
  SERIALIZE_FIELD(i32, maxBooms, 8)
  RANGE(0, 3)
  SERIALIZE_FIELD(Scalar, shake, 0.5f)
  SERIALIZE_FIELD(i32, booms, 6)
  SERIALIZE_FIELD(i32, crates, 5)

  i32 tick = 0, count = 0, slot = 0, doneTick = -1;
  Scalar shakeT = 0, flash = 0, cx = 240, cy = 150;
  Vector3 camBase;
  u32 rng = 12345;
  i32 tint[8] = {};
  bool started = false, finished = false;
  InputAction* move = nullptr;

  Scalar rand01() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<Scalar>(rng >> 8) * (1.0f / 16777216.0f);
  }

  static GameObject* find(const char* base, i32 i) {
    char name[24];
    Format(name, sizeof name, "{0}{1}", base, i);
    return GameObject::Find(name);
  }

  void Start() override {
    move = InputActions::Find("Move");
    if (Camera* c = Camera::main()) camBase = c->transform()->position();
    started = true;
    Debug::Log("rensga: fx ready booms=%d crates=%d pointer=%s", static_cast<int>(booms), static_cast<int>(crates),
               bot ? "bot" : (Input::mousePresent() ? "mouse" : "nub"));
  }

  // The world point under a screen point on the ground plane y = 0. False when the ray does not reach the ground.
  bool groundPoint(Scalar sx, Scalar sy, Vector3* out) const {
    Camera* c = Camera::main();
    if (!c) return false;
    const Ray r = c->ScreenPointToRay(Vector2(sx, sy));
    if (r.direction.y > -0.01f) return false;
    *out = r.GetPoint(-r.origin.y / r.direction.y);
    return true;
  }

  void boom(Scalar sx, Scalar sy) {
    Vector3 p;
    if (!groundPoint(sx, sy, &p)) return;
    const i32 s = slot;
    slot = (slot + 1) % booms;
    ++count;
    const Vector3 at = p + Vector3(0, 0.3f, 0);
    static const char* kNames[3] = {"Fire", "Smoke", "Sparks"};
    static const i32 kCounts[3] = {36, 26, 44};
    for (i32 k = 0; k < 3; ++k) {
      GameObject* go = find(kNames[k], s);
      if (!go) continue;
      go->transform.setPosition(at);
      if (ParticleSystem* ps = go->GetComponent<ParticleSystem>()) {
        ps->Clear();
        ps->Emit(kCounts[k]);
      }
    }
    if (GameObject* m = GameObject::Find("Marker")) m->transform.setPosition(p);
    if (GameObject* l = GameObject::Find("BoomLight")) l->transform.setPosition(p + Vector3(0, 1.4f, 0));
    flash = 1.0f;
    shakeT = 1.0f;
    for (i32 i = 0; i < crates && i < 8; ++i) {
      GameObject* cr = find("Crate", i);
      if (!cr) continue;
      const Vector3 d = cr->transform.position() - p;
      if (d.magnitude() < 5.5f) tint[i] = 45;
    }
    Debug::DrawLine(p, p + Vector3(0, 4.0f, 0), Color32(255, 80, 60, 255), 0.5f);
    const Vector3 back = Camera::main()->WorldToScreenPoint(p);
    Debug::Log("rensga: fx boom=%d screen=%d,%d world=%d,%d back=%d,%d live=%u", static_cast<int>(count), static_cast<int>(sx),
               static_cast<int>(sy), static_cast<int>(p.x * 100), static_cast<int>(p.z * 100), static_cast<int>(back.x + 0.5f),
               static_cast<int>(back.y + 0.5f), static_cast<unsigned>(Particles::live()));
  }

  void FixedUpdate() override {
    ++tick;
    flash = flash > 0 ? flash - Time::fixedDeltaTime() * 2.5f : 0;
    if (flash < 0) flash = 0;
    if (GameObject* l = GameObject::Find("BoomLight"))
      if (Light* lt = l->GetComponent<Light>()) lt->intensity = flash * 3.0f;
    // Crate tints: a MaterialPropertyBlock per crate, the shared material stays untouched.
    for (i32 i = 0; i < crates && i < 8; ++i) {
      if (tint[i] <= 0) continue;
      GameObject* cr = find("Crate", i);
      MeshRenderer* mr = cr ? cr->GetComponent<MeshRenderer>() : nullptr;
      if (!mr) continue;
      --tint[i];
      if (tint[i] == 0) {
        mr->ClearPropertyBlock();
      } else {
        const Scalar t = static_cast<Scalar>(tint[i]) / 45.0f;
        MaterialPropertyBlock b;
        b.SetColor(Color32(255, static_cast<u8>(255 - 170 * t), static_cast<u8>(255 - 190 * t), 255));
        mr->SetPropertyBlock(b);
      }
    }
    if (bot) {
      static const i32 kPts[8][2] = {{240, 125}, {120, 150}, {360, 160}, {200, 210}, {320, 118}, {80, 190}, {260, 172}, {410, 205}};
      cx = static_cast<Scalar>(kPts[count % 8][0]);
      cy = static_cast<Scalar>(kPts[count % 8][1]);
      if (count < maxBooms && tick % interval == 12) boom(cx, cy);
      else if (count >= maxBooms && !finished && tick % interval == 12) storm();
      return;
    }
    if (Input::mousePresent()) {
      const Vector2 m = Input::mousePosition();
      cx = m.x;
      cy = m.y;
      if (Input::GetMouseButtonDown(0)) boom(cx, cy);
    } else {
      const Vector2 mv = move ? move->ReadValue<Vector2>() : Vector2(0, 0);
      cx = Mathf::Clamp(cx + mv.x * 200.0f * Time::fixedDeltaTime(), 8.0f, 472.0f);
      cy = Mathf::Clamp(cy - mv.y * 200.0f * Time::fixedDeltaTime(), 110.0f, 264.0f);
      if (Input::GetButtonDown("Jump")) boom(cx, cy);
    }
  }

  // The bot's last act: every set of systems is emitted again and again far beyond its maxParticles.
  void storm() {
    finished = true;
    for (i32 round = 0; round < 6; ++round) {
      for (i32 s = 0; s < booms; ++s) {
        for (const char* n : {"Fire", "Smoke", "Sparks"}) {
          GameObject* go = find(n, s);
          ParticleSystem* ps = go ? go->GetComponent<ParticleSystem>() : nullptr;
          if (!ps) continue;
          go->transform.setPosition(Vector3(static_cast<Scalar>(s * 2 - 5), 0.4f, -1.0f));
          ps->Emit(60);
        }
      }
    }
    Debug::Log("rensga: fx done booms=%d live=%u reserved=%u rejected=%u", static_cast<int>(count), static_cast<unsigned>(Particles::live()),
               static_cast<unsigned>(Particles::reserved()), static_cast<unsigned>(Particles::rejected()));
    doneTick = tick;
  }

  void Update() override {
    // The cursor: a small cross in virtual pixels.
    const int x = static_cast<int>(cx), y = static_cast<int>(cy);
    const Color32 white(255, 255, 255, 230);
    Hud::Quad(x - 8, y, 5, 1, white);
    Hud::Quad(x + 4, y, 5, 1, white);
    Hud::Quad(x, y - 8, 1, 5, white);
    Hud::Quad(x, y + 4, 1, 5, white);
  }

  void LateUpdate() override {
    Camera* c = Camera::main();
    if (!c) return;
    Vector3 off;
    if (shakeT > 0) {
      const Scalar a = shake * shakeT * shakeT;
      off = Vector3((rand01() - 0.5f) * 2.0f * a, (rand01() - 0.5f) * 2.0f * a, 0);
      shakeT -= Time::deltaTime() * 1.6f;
      if (shakeT < 0) shakeT = 0;
    }
    c->transform()->setPosition(camBase + off);
  }
};
