// F6 (A3, API 12): Budget queries. Limits come from the CTGT record of the bound pack; used figures from the active World's arena and
// the counters the runtime keeps (rensga/core/runtime_stats.h). 0 = unknown.
#include "rensga/Budget.h"
#include "rensga/core/runtime_stats.h"
#include "rensga/core/scene_manager.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {
const PackConsoleTarget* target() {
  const PackView* pk = SceneManager::pack();
  return pk ? pk->records<PackConsoleTarget>(pack_section::CTGT) : nullptr;
}
u32 percent(u32 used, u32 limit) { return (limit && used) ? static_cast<u32>((static_cast<u64>(used) * 100u) / limit) : 0; }
}  // namespace

u32 Budget::memoryUsed() {
  World* w = World::active();
  return w ? static_cast<u32>(w->arena().used()) : 0;
}
u32 Budget::memoryLimit() {
  const PackConsoleTarget* t = target();
  return t ? t->ramBytes : 0;
}
u32 Budget::vramUsed() { return runtime_stats::vram(); }
u32 Budget::vramLimit() {
  const PackConsoleTarget* t = target();
  return t ? t->vramBytes : 0;
}
u32 Budget::trianglesLastFrame() { return runtime_stats::triangles(); }
u32 Budget::trianglesLimit() {
  const PackConsoleTarget* t = target();
  return t ? t->trianglesPerFrame : 0;
}
u32 Budget::voicesUsed() { return runtime_stats::voicesUsed(); }
u32 Budget::voicesLimit() {
  const u32 v = runtime_stats::voiceLimit();
  if (v) return v;
  const PackConsoleTarget* t = target();
  return t ? t->audioVoices : 0;
}
u32 Budget::percentUsed() {
  const u32 m = percent(memoryUsed(), memoryLimit());
  const u32 v = percent(vramUsed(), vramLimit());
  return m > v ? m : v;
}

}  // namespace rensga
