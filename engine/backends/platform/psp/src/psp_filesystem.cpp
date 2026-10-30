#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

#include "rensga/platform/psp.h"

namespace rensga {

PspFileSystem::PspFileSystem(const char* root) {
  strncpy(root_, root, sizeof(root_) - 1);
  root_[sizeof(root_) - 1] = 0;
  for (int& fd : fds_) fd = -1;
}

bool PspFileSystem::resolve(const char* path, char* out, unsigned outSize) const {
  if (!path) return false;
  const char* rel = path;
  if (!strncmp(path, "data:/", 6) || !strncmp(path, "save:/", 6)) rel = path + 6;
  else if (strchr(path, ':')) return snprintf(out, outSize, "%s", path) < static_cast<int>(outSize);  // explicit device
  if (strstr(rel, "..")) return false;
  int n = snprintf(out, outSize, "%s%s", root_, rel);
  return n > 0 && static_cast<unsigned>(n) < outSize;
}

Span<const u8> PspFileSystem::readFile(const char* path, Arena& arena) {
  char full[160];
  if (!resolve(path, full, sizeof(full))) {
    RLOG_ERROR("fs", "bad path %s", path ? path : "(null)");
    return {};
  }
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

i32 PspFileSystem::openStream(const char* path) {
  char full[160];
  if (!resolve(path, full, sizeof(full))) return -1;
  for (int i = 0; i < kStreamSlots; ++i) {
    if (fds_[i] >= 0) continue;
    SceUID fd = sceIoOpen(full, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    fds_[i] = fd;
    return i + 1;
  }
  RLOG_ERROR("fs", "all %d stream slots are in use", kStreamSlots);
  return -1;
}

u32 PspFileSystem::readStream(i32 h, void* dst, u32 bytes) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return 0;
  int r = sceIoRead(fds_[h - 1], dst, bytes);
  return r > 0 ? static_cast<u32>(r) : 0;
}

bool PspFileSystem::seekStream(i32 h, u32 offset) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return false;
  return sceIoLseek(fds_[h - 1], static_cast<SceOff>(offset), PSP_SEEK_SET) >= 0;
}

void PspFileSystem::closeStream(i32 h) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return;
  sceIoClose(fds_[h - 1]);
  fds_[h - 1] = -1;
}

bool PspFileSystem::writeFile(const char* path, const void* data, u32 bytes) {
  char full[160];
  if (!resolve(path, full, sizeof(full))) return false;
  SceUID fd = sceIoOpen(full, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd < 0) return false;
  int put = bytes ? sceIoWrite(fd, data, bytes) : 0;
  sceIoClose(fd);
  return put == static_cast<int>(bytes);
}

}  // namespace rensga
