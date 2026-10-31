// F5.13: application events. pumpApplicationEvents() runs once per loop iteration: it asks the bound platform
// (IPlatform::suspendState) whether the system suspended or the Home popup opened, and sends Behaviour::OnApplicationPause(true / false)
// to every started behaviour. Defined in engine/core/src/app_events.cpp.
#pragma once
#include "rensga/core/world.h"

namespace rensga {
void pumpApplicationEvents(World& world);
}  // namespace rensga
