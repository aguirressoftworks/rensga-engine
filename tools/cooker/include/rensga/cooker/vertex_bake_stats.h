// F6.02 (B3): progress and results of baked vertex lighting (tools/cooker/src/vertex_bake.h), readable from another thread. The
// editor's Lighting window shows them while the scene cooks on a worker.
#pragma once
#include <cstdint>

namespace rensga {
namespace cooker {

// Progress and results of the bakes of the running (or last) cook, readable from another thread (the editor's Bake button shows a
// progress line while it cooks on a worker). `vertices` counts the vertices of every baked mesh of this cook; `done` how many of them
// have their color. Results of a mesh whose inputs did not change are taken from a process-wide cache (`cacheHits` meshes).
struct BakeStats {
  uint64_t meshes = 0, vertices = 0, rays = 0, cacheHits = 0;
  double ms = 0;
};
void bakeProgressBegin(uint64_t plannedVertices);   // zeroes the counters; called by the scene bake when the cook starts
uint64_t bakeVerticesPlanned();
uint64_t bakeVerticesDone();
BakeStats lastBakeStats();
BakeStats lastBakeStatsThisThread();  // the stats of the last bake scene that was destroyed on this thread (one cook)
void bakeCacheClear();


}  // namespace cooker
}  // namespace rensga
