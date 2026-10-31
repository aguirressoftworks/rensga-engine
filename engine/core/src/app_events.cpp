// F5.13: sends OnApplicationPause when the bound platform reports a suspend / Home / resume transition (stub-sized on
// purpose; agent A5 owns the platform side and may extend this file).
#include "rensga/core/app_events.h"

#include "rensga/core/system.h"

namespace rensga {

namespace {
constexpr u32 kQ = 8;
bool g_q[kQ];
u32 g_head = 0, g_count = 0, g_resumes = 0;
}  // namespace

u32 applicationResumeCount() { return g_resumes; }

void injectApplicationPause(bool paused) {
  if (g_count == kQ) { g_head = (g_head + 1) % kQ; --g_count; }
  g_q[(g_head + g_count) % kQ] = paused;
  ++g_count;
}

void pumpApplicationEvents(World& world) {
  while (g_count) {
    const bool v = g_q[g_head];
    g_head = (g_head + 1) % kQ;
    --g_count;
    if (!v) ++g_resumes;
    world.sendApplicationPause(v);
  }
  IPlatform* p = system_services::platform();
  if (!p) return;
  for (int i = 0; i < 8; ++i) {  // bounded: a platform that keeps answering cannot stall the frame
    const i32 s = p->suspendState();
    if (s == 0) break;
    if (s < 0) ++g_resumes;
    world.sendApplicationPause(s > 0);
  }
}

}  // namespace rensga
