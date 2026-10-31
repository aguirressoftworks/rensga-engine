// F4.03: memory budget helpers shared by the runtime mains (additive to the frozen runtime_boot.h).
//
// The arena that holds the pack is sized from the pack's own CTGT (ramBytes), but the pack is read into that arena. So the
// main first peeks the CTGT record with a few small stream reads (header + section table + 48 bytes), creates the arena,
// then calls bootPack. Allocation-free; uses the IFileSystem stream API only.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/memory.h"
#include "rensga/core/pack_format.h"

namespace rensga {

// Reads the CTGT record of the pack at `path` without loading the pack. False if the pack, its header or its CTGT is
// missing (a pack cooked before CTGT existed): the caller then uses its own default budget.
bool peekPackTarget(IFileSystem& fs, const char* path, PackConsoleTarget& out);

// Arena size for a target: MemoryConfig::fromRamBudget(ramBytes).arenaBytes, clamped to `maxBytes` (0 = no clamp) with a warning.
size_t arenaBytesForRam(size_t ramBytes, size_t maxBytes);

// Logs "rensga: mem budget=<ramBytes> used=<arena bytes in use> arena=<arena capacity> peak=<high water>" (channel "game").
// `ramBytes` is the target's RAM budget; call it after boot and again at exit to see the peak.
void logMemBudget(size_t ramBytes, const Arena& arena);

}  // namespace rensga
