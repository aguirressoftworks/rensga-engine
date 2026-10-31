// F5.13: application events. pumpApplicationEvents() runs once per loop iteration: it asks the bound platform
// (IPlatform::suspendState) whether the system suspended or the Home popup opened, and sends Behaviour::OnApplicationPause(true / false)
// to every started behaviour. Defined in engine/core/src/app_events.cpp.
#pragma once
#include "rensga/core/types.h"
#include "rensga/core/world.h"

namespace rensga {
void pumpApplicationEvents(World& world);
// Queues an OnApplicationPause(paused) as if the platform had reported a suspend / Home (true) or a resume (false). Delivered by the
// next pumpApplicationEvents. For the dev-control 'apppause 0|1' command, tests and platforms that cannot raise the event themselves.
// Small fixed queue, no heap; when full the oldest entry is dropped.
void injectApplicationPause(bool paused);
// How many resume events (OnApplicationPause(false)) have been delivered so far. A main loop compares it between frames and
// resets its frame clock when it changed, so the time spent paused does not become one huge delta.
u32 applicationResumeCount();
}  // namespace rensga
