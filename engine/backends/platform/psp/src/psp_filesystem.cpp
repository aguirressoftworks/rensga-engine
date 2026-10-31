#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>

#include "rensga/platform/psp.h"

namespace rensga {

// ---- read-ahead streams -------------------------------------------------------------------------------------------
// One worker thread fills a static ring per read-ahead stream. The main thread only copies out of the ring (or waits for
// the worker when it is empty). All fd operations (read, seek, close) happen on the worker so they never overlap; the
// main thread posts a seek or close request instead. A semaphore used as a mutex guards the slot fields; the worker
// holds it only to publish, never during sceIoRead.
namespace {

struct Ahead {
  enum State : int { Free = 0, Active = 1, Closing = 2 };
  volatile int state = Free;
  int fd = -1;
  volatile bool eof = false, seekPending = false;
  volatile u32 seekTo = 0;
  volatile u32 gen = 0;           // bumps on every seek; a chunk read under an older generation is dropped
  volatile u32 head = 0, count = 0;  // ring read position and bytes held
  alignas(16) u8 ring[PspFileSystem::kAheadBytes];
};

Ahead g_ahead[PspFileSystem::kAheadStreams];
SceUID g_lock = -1, g_work = -1, g_data = -1, g_thread = -1;
constexpr u32 kChunk = 8 * 1024;
alignas(16) u8 g_stage[kChunk];

void lockA() { sceKernelWaitSema(g_lock, 1, nullptr); }
void unlockA() { sceKernelSignalSema(g_lock, 1); }

int workerMain(SceSize, void*) {
  for (;;) {
    SceUInt to = 20000;  // wake on a request, or every 20 ms to top rings up
    sceKernelWaitSema(g_work, 1, &to);
    bool didWork = true;
    while (didWork) {
      didWork = false;
      for (int i = 0; i < PspFileSystem::kAheadStreams; ++i) {
        Ahead& a = g_ahead[i];
        if (a.state == Ahead::Free) continue;
        if (a.state == Ahead::Closing) {
          if (a.fd >= 0) sceIoClose(a.fd);
          lockA();
          a.fd = -1; a.count = 0; a.head = 0; a.eof = false; a.seekPending = false;
          a.state = Ahead::Free;
          unlockA();
          didWork = true;
          continue;
        }
        if (a.seekPending) {
          lockA();
          const u32 target = a.seekTo, gen = a.gen;
          a.seekPending = false;
          unlockA();
          sceIoLseek(a.fd, static_cast<SceOff>(target), PSP_SEEK_SET);
          (void)gen;
          didWork = true;
          continue;
        }
        if (a.eof) continue;
        lockA();
        const u32 space = PspFileSystem::kAheadBytes - a.count;
        const u32 gen = a.gen;
        unlockA();
        if (space < kChunk) continue;
        const int got = sceIoRead(a.fd, g_stage, kChunk);
        lockA();
        if (a.gen == gen && a.state == Ahead::Active && !a.seekPending) {
          if (got <= 0) {
            a.eof = true;
          } else {
            u32 pos = (a.head + a.count) & (PspFileSystem::kAheadBytes - 1);
            for (int k = 0; k < got; ++k) {
              a.ring[pos] = g_stage[k];
              pos = (pos + 1) & (PspFileSystem::kAheadBytes - 1);
            }
            a.count += static_cast<u32>(got);
            if (got < static_cast<int>(kChunk)) a.eof = true;
          }
        }
        unlockA();
        sceKernelSignalSema(g_data, 1);
        didWork = true;
      }
    }
  }
  return 0;
}

bool startWorker() {
  if (g_thread >= 0) return true;
  g_lock = sceKernelCreateSema("rensga ahead lock", 0, 1, 1, nullptr);
  g_work = sceKernelCreateSema("rensga ahead work", 0, 0, 64, nullptr);
  g_data = sceKernelCreateSema("rensga ahead data", 0, 0, 64, nullptr);
  if (g_lock < 0 || g_work < 0 || g_data < 0) return false;
  g_thread = sceKernelCreateThread("rensga stream", workerMain, 0x30, 0x4000, PSP_THREAD_ATTR_USER, nullptr);
  if (g_thread < 0) return false;
  sceKernelStartThread(g_thread, 0, nullptr);
  RLOG_INFO("fs", "rensga: music stream thread started (%d read-ahead streams of %u KB)", PspFileSystem::kAheadStreams,
            PspFileSystem::kAheadBytes / 1024);
  return true;
}

}  // namespace

static const char* const kProbeNames[] = {"argv0", "ms0", "umd0", "host0"};

void pspProbeDataRoot(const char* argv0, const char* probeFile, char* out, unsigned outSize) {
  char cand[4][128];
  for (auto& c : cand) c[0] = 0;
  if (argv0 && strchr(argv0, ':')) {
    snprintf(cand[0], sizeof(cand[0]), "%s", argv0);
    char* slash = strrchr(cand[0], '/');
    if (slash) slash[1] = 0; else cand[0][0] = 0;
    char* colon = strchr(cand[0], ':');  // "umd0://" -> "umd0:/" (PPSSPP hands the ELF path with a doubled slash)
    while (colon && colon[1] == '/' && colon[2] == '/') memmove(colon + 2, colon + 3, strlen(colon + 3) + 1);
  }
  snprintf(cand[1], sizeof(cand[1]), "ms0:/PSP/GAME/%s/", RENSGA_PSP_GAME_ID);
  snprintf(cand[2], sizeof(cand[2]), "umd0:/PSP_GAME/USRDIR/");
  snprintf(cand[3], sizeof(cand[3]), "host0:/");
  const char* chosen = nullptr;
  for (int i = 0; i < 4; ++i) {
    if (!cand[i][0]) continue;
    char path[192];
    snprintf(path, sizeof(path), "%s%s", cand[i], probeFile);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    const bool found = fd >= 0;
    if (found) sceIoClose(fd);
    RLOG_INFO("fs", "rensga: data probe %s %s (%s)", cand[i], found ? "found" : "missing", kProbeNames[i]);
    if (found && !chosen) chosen = cand[i];
  }
  const char* root = chosen ? chosen : RENSGA_PSP_DEFAULT_ROOT;
  snprintf(out, outSize, "%s", root);
  RLOG_INFO("fs", "rensga: data root=%s (%s)", out, chosen ? "found" : "nothing found, build default");
}

PspFileSystem::PspFileSystem(const char* root) {
  strncpy(root_, root, sizeof(root_) - 1);
  root_[sizeof(root_) - 1] = 0;
  for (int& fd : fds_) fd = -1;
  for (signed char& a : ahead_) a = -1;
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
    if (fds_[i] >= 0 || ahead_[i] >= 0) continue;
    SceUID fd = sceIoOpen(full, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    int ai = -1;
    if (startWorker()) {
      for (int k = 0; k < kAheadStreams && ai < 0; ++k)
        if (g_ahead[k].state == Ahead::Free) ai = k;
    }
    if (ai < 0) {
      fds_[i] = fd;
      return i + 1;
    }
    Ahead& a = g_ahead[ai];
    lockA();
    a.fd = fd; a.head = 0; a.count = 0; a.eof = false; a.seekPending = false; ++a.gen;
    a.state = Ahead::Active;
    unlockA();
    ahead_[i] = static_cast<signed char>(ai);
    sceKernelSignalSema(g_work, 1);
    return i + 1;
  }
  RLOG_ERROR("fs", "all %d stream slots are in use", kStreamSlots);
  return -1;
}

u32 PspFileSystem::readStream(i32 h, void* dst, u32 bytes) {
  if (h < 1 || h > kStreamSlots) return 0;
  const int slot = h - 1;
  if (ahead_[slot] >= 0) {
    Ahead& a = g_ahead[ahead_[slot]];
    u8* out = static_cast<u8*>(dst);
    u32 done = 0;
    int stalls = 0;
    while (done < bytes) {
      lockA();
      u32 take = bytes - done < a.count ? bytes - done : a.count;
      if (!a.seekPending) {
        for (u32 k = 0; k < take; ++k) {
          out[done + k] = a.ring[(a.head + k) & (kAheadBytes - 1)];
        }
        a.head = (a.head + take) & (kAheadBytes - 1);
        a.count -= take;
        done += take;
      }
      const bool eof = a.eof && a.count == 0 && !a.seekPending;
      unlockA();
      if (take) sceKernelSignalSema(g_work, 1);
      if (done >= bytes || eof) break;
      SceUInt to = 100000;  // the worker is reading; wait for the next chunk
      if (sceKernelWaitSema(g_data, 1, &to) < 0 && ++stalls > 20) {
        RLOG_ERROR("fs", "rensga: music stream read stalled for 2 s, giving up");
        break;
      }
    }
    return done;
  }
  if (fds_[slot] < 0) return 0;
  int r = sceIoRead(fds_[slot], dst, bytes);
  return r > 0 ? static_cast<u32>(r) : 0;
}

bool PspFileSystem::seekStream(i32 h, u32 offset) {
  if (h < 1 || h > kStreamSlots) return false;
  const int slot = h - 1;
  if (ahead_[slot] >= 0) {
    Ahead& a = g_ahead[ahead_[slot]];
    lockA();
    a.seekTo = offset; a.seekPending = true; ++a.gen; a.count = 0; a.head = 0; a.eof = false;
    unlockA();
    sceKernelSignalSema(g_work, 1);
    return true;
  }
  if (fds_[slot] < 0) return false;
  return sceIoLseek(fds_[slot], static_cast<SceOff>(offset), PSP_SEEK_SET) >= 0;
}

void PspFileSystem::closeStream(i32 h) {
  if (h < 1 || h > kStreamSlots) return;
  const int slot = h - 1;
  if (ahead_[slot] >= 0) {
    Ahead& a = g_ahead[ahead_[slot]];
    lockA();
    a.state = Ahead::Closing; ++a.gen;
    unlockA();
    sceKernelSignalSema(g_work, 1);
    // Wait until the worker closed the fd, so the ring is not reused while a read is in flight.
    for (int i = 0; i < 100 && a.state != Ahead::Free; ++i) sceKernelDelayThread(1000);
    ahead_[slot] = -1;
    return;
  }
  if (fds_[slot] < 0) return;
  sceIoClose(fds_[slot]);
  fds_[slot] = -1;
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
