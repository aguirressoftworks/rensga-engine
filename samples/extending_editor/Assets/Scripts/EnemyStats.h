// F6.07 data asset: the tuning values of one kind of enemy. Create one with Project > Create > Data Asset > EnemyStats, edit it in the
// Inspector, then drag it onto the `stats` field of a Patroller. The values are cooked into the pack's DATA section.
#pragma once
#include <rensga/Rensga.h>

using namespace rensga;

struct EnemyStats : DataAsset {
  REFLECT(EnemyStats)
  HEADER("Identity")
  SERIALIZE_FIELD(String32, label, "Grunt")
  SERIALIZE_ENUM(kind, 0, "Walker|Scout|Brute")
  HEADER("Tuning")
  TOOLTIP("Hit points; 0 would be dead on arrival")
  RANGE(1, 200)
  SERIALIZE_FIELD(i32, health, 30)
  TOOLTIP("Units per second along the route")
  RANGE(0.25, 12)
  SERIALIZE_FIELD(Scalar, speed, 2.0f)
  SERIALIZE_FIELD(Color32, tint, Color32(220, 80, 60, 255))
  TOOLTIP("Seconds to wait at each end of the route (up to four values, the first is used)")
  SERIALIZE_ARRAY(Scalar, pauses, 4)
};
