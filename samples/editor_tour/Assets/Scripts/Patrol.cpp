#include <rensga/Rensga.h>

using namespace rensga;

// API 11 exercise "patrol script with gizmos": walks its object around up to four waypoints.
// The Scene view shows the route (OnDrawGizmos) and, when the object is selected, its next stop (OnDrawGizmosSelected).
// OnValidate keeps the data sane after any inspector edit: at least 2 and at most 4 waypoints, a speed that moves, and
// waypoints above the ground. It runs on a detached copy, so it touches only this component's own fields.
struct Patrol : Behaviour {
  REFLECT(Patrol)
  HEADER("Movement")
  TOOLTIP("Units per second. OnValidate keeps it at 0.25 or more.")
  RANGE(0, 10)
  SERIALIZE_FIELD(Scalar, speed, 2.0f)
  TOOLTIP("Seconds to wait at every waypoint")
  RANGE(0, 5)
  SERIALIZE_FIELD(Scalar, pause, 0.5f)
  HEADER("Waypoints")
  TOOLTIP("How many of the four waypoints are used (OnValidate clamps it to 2..4)")
  RANGE(0, 8)
  SERIALIZE_FIELD(i32, waypointCount, 3)
  SERIALIZE_FIELD(Vector3, waypoint0, Vector3(-3, 0.5f, -2))
  SERIALIZE_FIELD(Vector3, waypoint1, Vector3(3, 0.5f, -2))
  SERIALIZE_FIELD(Vector3, waypoint2, Vector3(0, 0.5f, 3))
  SERIALIZE_FIELD(Vector3, waypoint3, Vector3(-3, 0.5f, 3))

  i32 next_ = 1;
  Scalar wait_ = 0;

  Vector3 point(i32 i) const {
    switch (i) {
      case 0: return waypoint0;
      case 1: return waypoint1;
      case 2: return waypoint2;
      default: return waypoint3;
    }
  }

  void OnValidate() override {
    if (waypointCount < 2) waypointCount = 2;
    if (waypointCount > 4) waypointCount = 4;
    if (speed < 0.25f) speed = 0.25f;
    if (waypoint0.y < 0) waypoint0.y = 0;
    if (waypoint1.y < 0) waypoint1.y = 0;
    if (waypoint2.y < 0) waypoint2.y = 0;
    if (waypoint3.y < 0) waypoint3.y = 0;
  }

  void OnDrawGizmos() override {
    const i32 n = waypointCount < 2 ? 2 : (waypointCount > 4 ? 4 : waypointCount);
    for (i32 i = 0; i < n; ++i) {
      Gizmos::setColor(Color(1.0f, 0.8f, 0.1f, 1.0f));
      Gizmos::DrawWireSphere(point(i), 0.25f);
      Gizmos::setColor(Color(0.3f, 0.9f, 0.4f, 1.0f));
      Gizmos::DrawLine(point(i), point((i + 1) % n));
    }
  }

  void OnDrawGizmosSelected() override {
    const i32 n = waypointCount < 2 ? 2 : (waypointCount > 4 ? 4 : waypointCount);
    const Vector3 here = transform()->position();
    Gizmos::setColor(Color(1.0f, 0.3f, 0.2f, 1.0f));
    Gizmos::DrawLine(here, point(next_ % n));
    Gizmos::DrawWireCube(here, Vector3(1.2f, 1.2f, 1.2f));
  }

  void Update() override {
    const i32 n = waypointCount < 2 ? 2 : (waypointCount > 4 ? 4 : waypointCount);
    if (wait_ > 0) {
      wait_ -= Time::deltaTime();
      return;
    }
    const Vector3 goal = point(next_ % n);
    const Vector3 here = transform()->position();
    const Vector3 d = goal - here;
    const Scalar dist = d.magnitude();
    const Scalar step = speed * Time::deltaTime();
    if (dist <= step) {
      transform()->setPosition(goal);
      next_ = (next_ + 1) % n;
      wait_ = pause;
    } else {
      transform()->setPosition(here + d * (step / dist));
    }
  }
};
