#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

#include "rensga/platform/psp.h"

namespace rensga {

PspFileSystem::PspFileSystem(const char* root) {
  strncpy(root_, root, sizeof(root_) - 1);
  root_[sizeof(root_) - 1] = 0;
}

Span<const u8> PspFileSystem::readFile(const char* path, Arena& arena) {
  char full[160];
  // A path with a device prefix ("host0:", "ms0:", "umd0:") is used as is.
  if (strchr(path, ':')) snprintf(full, sizeof(full), "%s", path);
  else snprintf(full, sizeof(full), "%s%s", root_, path);
  SceUID fd = sceIoOpen(full, PSP_O_RDONLY, 0);
  if (fd < 0) {
    RLOG_ERROR("fs", "cannot open %s (0x%08x)", full, static_cast<unsigned>(fd));
    return {};
  }
  SceOff n = sceIoLseek(fd, 0, PSP_SEEK_END);
  sceIoLseek(fd, 0, PSP_SEEK_SET);
  u8* mem = n > 0 ? static_cast<u8*>(arena.allocate(static_cast<size_t>(n))) : nullptr;
  if (!mem) {
    sceIoClose(fd);
    RLOG_ERROR("fs", "no arena space for %s (%d bytes)", full, static_cast<int>(n));
    return {};
  }
  size_t got = 0;
  while (got < static_cast<size_t>(n)) {
    int r = sceIoRead(fd, mem + got, static_cast<unsigned>(n) - got);
    if (r <= 0) break;
    got += static_cast<size_t>(r);
  }
  sceIoClose(fd);
  if (got != static_cast<size_t>(n)) return {};
  return Span<const u8>{mem, got};
}

void PspFileSystem::writeLog(const char* text) { RLOG_INFO("game", "%s", text); }

}  // namespace rensga
