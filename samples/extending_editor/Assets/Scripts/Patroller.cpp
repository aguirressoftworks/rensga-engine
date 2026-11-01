// F6.07 sample: a patrolling cube whose speed, colour and pause come from a data asset (EnemyStats). The Editor/ folder next to Assets/
// holds the editor extensions for this component: a custom inspector, a window and menu items.
//
// Log lines (grepped by tools/ci/extending_editor.sh):
//   rensga: patroller <name> label=<label> kind=<n> hp=<n> speed_cm=<n> pauses=<n>
//   rensga: patroller <name> no stats        (the Guid field is empty or not an EnemyStats)
//   rensga: patrol done <name> laps=<n> x_cm=<n>
#include <rensga/Rensga.h>

#include "EnemyStats.h"

using namespace rensga;

struct Patroller : Behaviour {
  REFLECT(Patroller)
  HEADER("Data")
  TOOLTIP("An EnemyStats data asset: Create > Data Asset > EnemyStats, then drag it here")
  SERIALIZE_FIELD(Guid, stats, Guid())
  HEADER("Route")
  SERIALIZE_FIELD(Vector3, pointA, Vector3(-3, 0.5f, 0))
  SERIALIZE_FIELD(Vector3, pointB, Vector3(3, 0.5f, 0))
  RANGE(0, 600)
  SERIALIZE_FIELD(i32, stopAfterFrames, 0)

  const EnemyStats* data = nullptr;  // read-only, valid for the whole run (DataAssets::Get)
  bool toB = true;
  Scalar wait = 0;
  i32 laps = 0, frames = 0;

  void Start() override {
    data = DataAssets::Get<EnemyStats>(stats);
    if (!data) {
      Debug::Log("rensga: patroller %s no stats", gameObject()->name);
      return;
    }
    MeshRenderer* mr = gameObject()->GetComponent<MeshRenderer>();
    if (mr) mr->color = data->tint;
    Debug::Log("rensga: patroller %s label=%s kind=%d hp=%d speed_cm=%d pauses=%d", gameObject()->name, data->label.c_str(),
               static_cast<int>(data->kind), static_cast<int>(data->health), static_cast<int>(data->speed * 100), static_cast<int>(data->pauses.count));
    transform()->setPosition(pointA);
  }

  void OnDrawGizmos() override {
    Gizmos::setColor(Color(0.3f, 0.9f, 0.4f, 1.0f));
    Gizmos::DrawLine(pointA, pointB);
    Gizmos::DrawWireSphere(pointA, 0.2f);
    Gizmos::DrawWireSphere(pointB, 0.2f);
  }

  void Update() override {
    if (!data) return;
    ++frames;
    if (stopAfterFrames > 0 && frames == stopAfterFrames)
      Debug::Log("rensga: patrol done %s laps=%d x_cm=%d", gameObject()->name, static_cast<int>(laps), static_cast<int>(transform()->position().x * 100));
    if (wait > 0) {
      wait -= Time::deltaTime();
      return;
    }
    const Vector3 goal = toB ? pointB : pointA;
    const Vector3 here = transform()->position();
    const Vector3 d = goal - here;
    const Scalar dist = d.magnitude();
    const Scalar step = data->speed * Time::deltaTime();
    if (dist <= step) {
      transform()->setPosition(goal);
      if (!toB) ++laps;
      toB = !toB;
      wait = data->pauses.count > 0 ? data->pauses.items[0] : Scalar(0);
    } else {
      transform()->setPosition(here + d * (step / dist));
    }
  }
};
