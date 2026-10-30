#include <stdio.h>
#include <string.h>

#include "rensga/core/backends.h"
#include "rensga/core/log.h"

namespace rensga {

namespace {
void copyStr(char* dst, size_t n, const char* src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}
}  // namespace

StdioFileSystem::StdioFileSystem(const char* root, const char* logPath, const char* saveRoot) {
  copyStr(root_, sizeof(root_), root);
  copyStr(saveRoot_, sizeof(saveRoot_), saveRoot ? saveRoot : root);
  copyStr(logPath_, sizeof(logPath_), logPath);
  for (i32 i = 0; i < kMaxStreams; ++i) streams_[i] = nullptr;
}

StdioFileSystem::~StdioFileSystem() {
  for (i32 i = 0; i < kMaxStreams; ++i) closeStream(i);
}

// "data:/x" and bare "x" -> dataRoot + x, "save:/x" -> saveRoot + x. ".." segments are refused in
// prefixed paths so a virtual path cannot leave its root.
bool StdioFileSystem::resolve(const char* path, char* out, u32 outSize, bool* isSave) const {
  if (!path) return false;
  const char* root = root_;
  *isSave = false;
  bool virtualPath = true;
  if (strncmp(path, "data:/", 6) == 0) path += 6;
  else if (strncmp(path, "save:/", 6) == 0) { path += 6; root = saveRoot_; *isSave = true; }
  else virtualPath = false;  // bare host path (tools, tests): used as given
  for (const char* p = path; virtualPath && *p; ++p)
    if (p[0] == '.' && p[1] == '.' && (p == path || p[-1] == '/' || p[-1] == '\\') && (p[2] == 0 || p[2] == '/' || p[2] == '\\')) return false;
  int n = snprintf(out, outSize, "%s%s", root, path);
  return n > 0 && static_cast<u32>(n) < outSize;
}

Span<const u8> StdioFileSystem::readFile(const char* path, Arena& arena) {
  char full[512];
  bool save;
  if (!resolve(path, full, sizeof(full), &save)) {
    RLOG_ERROR("fs", "bad path %s", path ? path : "(null)");
    return {};
  }
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

i32 StdioFileSystem::openStream(const char* path) {
  char full[512];
  bool save;
  if (!resolve(path, full, sizeof(full), &save)) return -1;
  i32 slot = -1;
  for (i32 i = 0; i < kMaxStreams; ++i)
    if (!streams_[i]) { slot = i; break; }
  if (slot < 0) {
    RLOG_ERROR("fs", "no free stream slot for %s", full);
    return -1;
  }
  FILE* f = fopen(full, "rb");
  if (!f) {
    RLOG_ERROR("fs", "cannot open %s", full);
    return -1;
  }
  streams_[slot] = f;
  return slot;
}

u32 StdioFileSystem::readStream(i32 h, void* dst, u32 bytes) {
  if (h < 0 || h >= kMaxStreams || !streams_[h] || !dst) return 0;
  return static_cast<u32>(fread(dst, 1, bytes, static_cast<FILE*>(streams_[h])));
}

bool StdioFileSystem::seekStream(i32 h, u32 offset) {
  if (h < 0 || h >= kMaxStreams || !streams_[h]) return false;
  return fseek(static_cast<FILE*>(streams_[h]), static_cast<long>(offset), SEEK_SET) == 0;
}

void StdioFileSystem::closeStream(i32 h) {
  if (h < 0 || h >= kMaxStreams || !streams_[h]) return;
  fclose(static_cast<FILE*>(streams_[h]));
  streams_[h] = nullptr;
}

bool StdioFileSystem::writeFile(const char* path, const void* data, u32 bytes) {
  char full[512];
  bool save;
  if (!resolve(path, full, sizeof(full), &save) || !save) {
    RLOG_ERROR("fs", "writeFile needs a save:/ path, got %s", path ? path : "(null)");
    return false;
  }
  FILE* f = fopen(full, "wb");
  if (!f) {
    RLOG_ERROR("fs", "cannot write %s", full);
    return false;
  }
  size_t put = bytes ? fwrite(data, 1, bytes, f) : 0;
  bool ok = fclose(f) == 0 && put == bytes;
  if (!ok) RLOG_ERROR("fs", "short write to %s", full);
  return ok;
}

}  // namespace rensga
