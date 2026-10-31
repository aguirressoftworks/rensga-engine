#include "runtime_budget.h"

#include "rensga/core/log.h"

namespace rensga {

bool peekPackTarget(IFileSystem& fs, const char* path, PackConsoleTarget& out) {
  const i32 h = fs.openStream(path);
  if (h < 0) return false;
  bool ok = false;
  PackHeader hdr;
  if (fs.readStream(h, &hdr, sizeof(hdr)) == sizeof(hdr) && hdr.magic[0] == 'R' && hdr.magic[1] == 'P' && hdr.magic[2] == 'A' &&
      hdr.magic[3] == 'K' && hdr.sectionCount > 0 && hdr.sectionCount <= 64) {
    PackSection table[64];
    const u32 tableBytes = hdr.sectionCount * static_cast<u32>(sizeof(PackSection));
    if (fs.readStream(h, table, tableBytes) == tableBytes) {
      for (u32 i = 0; i < hdr.sectionCount && !ok; ++i) {
        if (table[i].fourcc != pack_section::CTGT || table[i].size < sizeof(PackConsoleTarget)) continue;
        if (fs.seekStream(h, table[i].offset) && fs.readStream(h, &out, sizeof(out)) == sizeof(out)) ok = true;
      }
    }
  }
  fs.closeStream(h);
  return ok;
}

size_t arenaBytesForRam(size_t ramBytes, size_t maxBytes) {
  size_t bytes = MemoryConfig::fromRamBudget(ramBytes).arenaBytes;
  if (maxBytes && bytes > maxBytes) {
    RLOG_WARN("game", "rensga: mem arena clamped from %u to %u bytes (device limit)", static_cast<unsigned>(bytes),
              static_cast<unsigned>(maxBytes));
    bytes = maxBytes;
  }
  return bytes;
}

void logMemBudget(size_t ramBytes, const Arena& arena) {
  RLOG_INFO("game", "rensga: mem budget=%u used=%u arena=%u peak=%u", static_cast<unsigned>(ramBytes),
            static_cast<unsigned>(arena.used()), static_cast<unsigned>(arena.capacity()), static_cast<unsigned>(arena.highWater()));
}

}  // namespace rensga
