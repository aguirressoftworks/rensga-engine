// F6.08 determinism fixture scripts (tests/determinism/make_phys_project.py): a kinematic platform moved by MovePosition, a
// CharacterController that walks over an oriented ramp and past a capsule, and a probe whose raycast / spherecast / overlap results move a
// marker, so every query result lands in the state hash that determinism.sh phys compares between Linux and the PSP simulation.
#include <rensga/Rensga.h>

using namespace rensga;

struct PhysMover : Behaviour {
  REFLECT(PhysMover)
  SERIALIZE_FIELD(Scalar, range, 4.0f)
  SERIALIZE_FIELD(i32, period, 180)
  Vector3 origin;
  i32 tick = 0;
  bool ready = false;

  void FixedUpdate() override {
    Rigidbody* rb = gameObject()->GetComponent<Rigidbody>();
    if (!rb) return;
    if (!ready) { origin = transform()->position(); ready = true; }
    ++tick;
    const i32 t = tick % period, h = period / 2;
    const Scalar f = static_cast<Scalar>(t < h ? t : period - t) / static_cast<Scalar>(h);  // triangle wave 0..1..0, no libm
    rb->MovePosition(origin + Vector3(range * f, 0, 0));
  }
};

struct PhysHero : Behaviour {
  REFLECT(PhysHero)
  SERIALIZE_FIELD(Scalar, speed, 0.045f)
  i32 tick = 0;

  void FixedUpdate() override {
    CharacterController* cc = gameObject()->GetComponent<CharacterController>();
    if (!cc) return;
    ++tick;
    const Scalar dir = (tick / 150) % 2 == 0 ? -1.0f : 1.0f;  // walks -x up the ramp for 2.5 s, then back
    cc->Move(Vector3(speed * dir, 0, 0.004f));
    if (tick % 60 == 0)
      Debug::Log("rensga: hero tick=%d x=%d y=%d grounded=%d", static_cast<int>(tick), static_cast<int>(transform()->position().x * 100),
                 static_cast<int>(transform()->position().y * 100), cc->isGrounded() ? 1 : 0);
  }
};

struct PhysProbe : Behaviour {
  REFLECT(PhysProbe)
  i32 tick = 0;
  i32 rayHits = 0, castHits = 0, overlaps = 0;

  void FixedUpdate() override {
    ++tick;
    GameObject* marker = GameObject::Find("Marker");
    GameObject* marker2 = GameObject::Find("Marker2");
    // A ray sweeping along x from above: it lands on the floor, the ramp, the crates and the moving platform in turn.
    const Scalar x = -8.0f + 0.04f * static_cast<Scalar>(tick % 400);
    RaycastHit hit;
    if (Physics::Raycast(Vector3(x, 12.0f, 0.3f), Vector3(0, -1, 0), &hit, 40.0f)) {
      ++rayHits;
      if (marker) marker->transform.setPosition(hit.point + hit.normal * 0.25f);
    }
    RaycastHit sc;
    if (Physics::SphereCast(Vector3(x, 12.0f, -0.9f), 0.35f, Vector3(0, -1, 0), &sc, 40.0f, LayerMask(~(1u << 9)))) {
      ++castHits;
      if (marker2) marker2->transform.setPosition(sc.point + sc.normal * 0.4f);
    }
    GameObject* found[8];
    overlaps += static_cast<i32>(Physics::OverlapSphere(Vector3(x, 1.0f, 0), 1.5f, found, 8));
    if (marker2 && (tick % 7) == 0) {
      Vector3 p = marker2->transform.position();
      p.z += static_cast<Scalar>(overlaps % 5) * 0.01f;
      marker2->transform.setPosition(p);
    }
    if (tick % 120 == 0) Debug::Log("rensga: probe tick=%d rays=%d casts=%d overlaps=%d", static_cast<int>(tick), static_cast<int>(rayHits), static_cast<int>(castHits), static_cast<int>(overlaps));
  }
};
