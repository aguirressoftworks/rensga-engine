// F6.08 (A2, API 6): ProjectSettings/Physics.json (+ layer names from TagManager.json) -> the PHY3 pack section. Called from cook.cpp.
#pragma once
#include <string>
#include <vector>

#include "rensga/core/pack_format.h"

namespace rensga {
namespace cooker {

// Physics.json: {"format":1,"gravity":[0,-9.81,0],"ignore":[[6,7],["Player","Enemy"]],"queriesHitTriggers":true,
//                "bounceThreshold":2.0,"sleepThreshold":0.05}
// "ignore" pairs name layers by index, or by name as written in TagManager.json "layers". Everything not listed collides. The two
// thresholds are optional (defaults 2 and 0.05 m/s). Returns false and fills `error` on bad input. The cooker writes PHY3 only
// when the project has a Physics.json, so a project without it cooks to the same bytes as before F6.
bool cookPhysics3D(const std::string& physicsJson, const std::string& tagManagerJson, PackPhysics3D& out, std::string& error);

}  // namespace cooker
}  // namespace rensga
