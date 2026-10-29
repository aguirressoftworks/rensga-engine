#include <stdio.h>
#include <string.h>

#include "rensga/core/backends.h"
#include "rensga/core/log.h"

namespace rensga {

StdioFileSystem::StdioFileSystem(const char* root, const char* logPath) {
  strncpy(root_, root ? root : "", sizeof(root_) - 1);
  root_[sizeof(root_) - 1] = 0;
  strncpy(logPath_, logPath ? logPath : "", sizeof(logPath_) - 1);
  logPath_[sizeof(logPath_) - 1] = 0;
}

Span<const u8> StdioFileSystem::readFile(const char* path, Arena& arena) {
  char full[512];
  snprintf(full, sizeof(full), "%s%s", root_, path);
  FILE* f = fopen(full, "rb");
  if (!f) {
    RLOG_ERROR("fs", "cannot open %s", full);
    return {};
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) {
    fclose(f);
    return {};
  }
  u8* mem = static_cast<u8*>(arena.allocate(static_cast<size_t>(n) + 1));
  if (!mem) {
    fclose(f);
    return {};
  }
  size_t got = fread(mem, 1, static_cast<size_t>(n), f);
  fclose(f);
  if (got != static_cast<size_t>(n)) return {};
  return Span<const u8>{mem, got};
}

void StdioFileSystem::writeLog(const char* text) {
  if (!logPath_[0]) return;
  FILE* f = fopen(logPath_, "a");
  if (!f) return;
  fputs(text, f);
  fputc('\n', f);
  fclose(f);
}

}  // namespace rensga
