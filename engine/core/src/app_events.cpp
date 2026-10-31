// F5.13: sends OnApplicationPause when the bound platform reports a suspend / Home / resume transition (stub-sized on
// purpose; agent A5 owns the platform side and may extend this file).
#include "rensga/core/app_events.h"

#include "rensga/core/system.h"

namespace rensga {

void pumpApplicationEvents(World& world) {
  IPlatform* p = system_services::platform();
  if (!p) return;
  for (int i = 0; i < 8; ++i) {  // bounded: a platform that keeps answering cannot stall the frame
    const i32 s = p->suspendState();
    if (s == 0) break;
    world.sendApplicationPause(s > 0);
  }
}

}  // namespace rensga
