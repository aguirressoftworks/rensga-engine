#include "rensga/core/script_samples.h"

#include <string.h>

#include "rensga/core/system.h"
#include "rensga/core/time.h"

namespace rensga {
namespace script_samples {
namespace {
struct Entry {
  const char* name = nullptr;
  u32 curUs = 0, curCalls = 0;
  u32 lastUs = 0;
  u64 winUs = 0;
  u32 winCalls = 0, winMax = 0;
};
struct Open {
  Entry* entry = nullptr;
  u64 start = 0;
};
Entry g_entries[kMaxNames];
u32 g_count = 0;
Open g_stack[kMaxDepth];
u32 g_depth = 0;
u32 g_winFrames = 0;
u64 g_frame = 0;
bool g_dirty = false;
ClockUsFn g_clock = nullptr;

u64 nowUs() {
  if (g_clock) return g_clock();
  if (IPlatform* p = system_services::platform()) return static_cast<u64>(p->timeSeconds() * 1000000.0);
  return 0;
}

Entry* find(const char* name, bool create) {
  for (u32 i = 0; i < g_count; ++i)
    if (g_entries[i].name == name) return &g_entries[i];
  for (u32 i = 0; i < g_count; ++i)
    if (strcmp(g_entries[i].name, name) == 0) return &g_entries[i];
  if (!create || g_count >= kMaxNames) return nullptr;
  g_entries[g_count].name = name;
  return &g_entries[g_count++];
}

void roll() {
  for (u32 i = 0; i < g_count; ++i) {
    Entry& e = g_entries[i];
    e.lastUs = e.curUs;
    e.winUs += e.curUs;
    e.winCalls += e.curCalls;
    if (e.curUs > e.winMax) e.winMax = e.curUs;
    e.curUs = 0;
    e.curCalls = 0;
  }
  ++g_winFrames;
  g_dirty = false;
}

void syncFrame() {
  const u64 f = Time::frameCount();
  if (g_dirty && f != g_frame) roll();
  g_frame = f;
}
}  // namespace

void setClock(ClockUsFn clock) { g_clock = clock; }

void begin(const char* name) {
  if (!name) return;
  syncFrame();
  g_dirty = true;
  if (g_depth >= kMaxDepth) return;
  Entry* e = find(name, true);
  g_stack[g_depth].entry = e;  // null when the table is full: the sample is skipped
  g_stack[g_depth].start = nowUs();
  ++g_depth;
}

void end() {
  if (g_depth == 0) return;
  --g_depth;
  Entry* e = g_stack[g_depth].entry;
  if (!e) return;
  const u64 d = nowUs() - g_stack[g_depth].start;
  e->curUs += static_cast<u32>(d);
  ++e->curCalls;
}

u32 lastMicros(const char* name) {
  if (!name) return 0;
  syncFrame();
  Entry* e = find(name, false);
  return e ? e->lastUs : 0;
}

void endFrame() {
  if (g_dirty) roll();
}

u32 windowCount() { return g_count; }

bool windowEntry(u32 i, WindowEntry* out) {
  if (i >= g_count || !out) return false;
  out->name = g_entries[i].name;
  out->frames = g_winFrames;
  out->calls = g_entries[i].winCalls;
  out->totalUs = g_entries[i].winUs;
  out->maxFrameUs = g_entries[i].winMax;
  return true;
}

void resetWindow() {
  for (u32 i = 0; i < g_count; ++i) {
    g_entries[i].winUs = 0;
    g_entries[i].winCalls = 0;
    g_entries[i].winMax = 0;
  }
  g_winFrames = 0;
}

void reset() {
  for (u32 i = 0; i < kMaxNames; ++i) g_entries[i] = Entry();
  g_count = 0;
  g_depth = 0;
  g_winFrames = 0;
  g_dirty = false;
  g_frame = 0;
}

}  // namespace script_samples
}  // namespace rensga
