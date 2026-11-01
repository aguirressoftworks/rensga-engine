#include "rensga/core/runtime_stats.h"

#include "rensga/core/audio.h"

namespace rensga {
namespace runtime_stats {
namespace {
u32 g_tris = 0, g_vram = 0;
const AudioMixer* g_mixer = nullptr;
}  // namespace
void setTriangles(u32 t) { g_tris = t; }
u32 triangles() { return g_tris; }
void setVram(u32 v) { g_vram = v; }
u32 vram() { return g_vram; }
void bindMixer(const AudioMixer* m) { g_mixer = m; }
u32 voicesUsed() { return g_mixer ? g_mixer->activeVoices() : 0; }
u32 voiceLimit() { return g_mixer ? g_mixer->voiceLimit() : 0; }
void reset() {
  g_tris = g_vram = 0;
  g_mixer = nullptr;
}
}  // namespace runtime_stats
}  // namespace rensga
