// PS2 platform (EE side). Boot sequence in Ps2Platform::init():
//   1. SifInitRpc(0)            start the EE <-> IOP RPC layer.
//   2. IOP reset (disc/USB boots only): SifIopReset("", 0); SifIopSync(); SifInitRpc(0).
//      Reloads the BIOS default IOP image (fioman, cdvdman, ...) so no loader leftovers stay on the IOP.
//      Skipped for host: boots because the loader's host driver lives on the IOP and would be lost.
//   3. SifLoadModule("rom0:XSIO2MAN") + ("rom0:XPADMAN"), falling back to "rom0:SIO2MAN" + "rom0:PADMAN"
//      (older BIOSes). They are in the BIOS ROM, so no IRX files are shipped.
//   4. padInit(0), padPortOpen(0,0,buf), wait for a stable state, request DualShock analog mode.
//   5. rom0:ROMVER -> video region (PAL when the region letter is 'E').
// USB (mass:) needs usbd/usbmass IRXs, which are not in the BIOS; a launcher embeds them (F8).
#include "rensga/platform/ps2.h"

#include <fcntl.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>
#include <unistd.h>

#include "rensga/core/log.h"

namespace rensga {

static constexpr unsigned long long kEeCpuHz = 294912000ULL;  // EE core clock (kBUSCLK * 2)
alignas(64) static char g_padArea[256];

static bool probeDisc() {
  int fd = open("cdrom0:\\SYSTEM.CNF;1", O_RDONLY);
  if (fd < 0) return false;
  close(fd);
  return true;
}

bool Ps2Platform::init() {
  SifInitRpc(0);
  if (probeDisc()) {
    SifIopReset("", 0);
    while (!SifIopSync()) {}
    SifInitRpc(0);
  }
  int sio = SifLoadModule("rom0:XSIO2MAN", 0, nullptr);
  int pm = sio >= 0 ? SifLoadModule("rom0:XPADMAN", 0, nullptr) : -1;
  if (sio < 0 || pm < 0) {
    sio = SifLoadModule("rom0:SIO2MAN", 0, nullptr);
    pm = sio >= 0 ? SifLoadModule("rom0:PADMAN", 0, nullptr) : -1;
  }
  if (sio >= 0 && pm >= 0 && padInit(0) == 1 && padPortOpen(0, 0, g_padArea) == 1) {
    padInit_ = true;
    info_.padOpen = true;
    for (int i = 0; i < 100000; ++i) {  // bounded wait: no pad plugged in is fine
      int st = padGetState(0, 0);
      if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) break;
    }
  } else {
    RLOG_WARN("ps2", "pad modules not available (sio2man=%d padman=%d)", sio, pm);
  }
  int fd = open("rom0:ROMVER", O_RDONLY);
  if (fd >= 0) {
    int n = read(fd, info_.romVer, 16);
    close(fd);
    if (n >= 5) {
      info_.romVer[n < 16 ? n : 16] = 0;
      info_.region = info_.romVer[4];
      info_.pal = info_.region == 'E';
    }
  }
  RLOG_INFO("ps2", "romver=%s region=%c video=%s pad=%d", info_.romVer, info_.region, info_.pal ? "PAL" : "NTSC", info_.padOpen ? 1 : 0);
  return true;
}

void Ps2Platform::pollInput(VirtualGamepad& pad) {
  pad = VirtualGamepad();
  if (!padInit_) return;
  int st = padGetState(0, 0);
  if (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1) return;
  if (!padModeSet_) {
    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);  // analog sticks; digital pads ignore it
    padModeSet_ = true;
  }
  padButtonStatus d;
  if (padRead(0, 0, &d) == 0) return;
  unsigned b = 0xFFFFu ^ d.btns;  // libpad bits are active low
  VirtualGamepad::Buttons& h = pad.held;
  h.south = b & PAD_CROSS;
  h.east = b & PAD_CIRCLE;
  h.west = b & PAD_SQUARE;
  h.north = b & PAD_TRIANGLE;
  h.l = b & PAD_L1;
  h.r = b & PAD_R1;
  h.start = b & PAD_START;
  h.select = b & PAD_SELECT;
  h.up = b & PAD_UP;
  h.down = b & PAD_DOWN;
  h.left = b & PAD_LEFT;
  h.right = b & PAD_RIGHT;
  if ((d.mode >> 4) == 7) {  // DualShock analog mode
    auto nub = [](unsigned char v) -> Scalar {
      Scalar f = (static_cast<Scalar>(v) - 128.0f) / 127.0f;
      if (f > -0.15f && f < 0.15f) return 0;
      return f > 1 ? 1 : (f < -1 ? -1 : f);
    };
    pad.leftX = nub(d.ljoy_h);
    pad.leftY = -nub(d.ljoy_v);  // stick Y grows downward; the pad's +Y is up
  }
}

double Ps2Platform::timeSeconds() {
  unsigned now = cpu_ticks();
  if (!clockStarted_) {
    clockStarted_ = true;
    lastTick_ = now;
  }
  ticks_ += static_cast<unsigned>(now - lastTick_);  // unsigned wrap-around is the delta
  lastTick_ = now;
  return static_cast<double>(ticks_) / static_cast<double>(kEeCpuHz);
}

// ---- file system -----------------------------------------------------------

Ps2FileSystem::Ps2FileSystem(Ps2Device device) : device_(device) {
  for (int& fd : fds_) fd = -1;
  if (device_ == Ps2Device::Auto) device_ = probeDisc() ? Ps2Device::Disc : Ps2Device::Host;
}

bool Ps2FileSystem::resolve(const char* path, char* out, unsigned outSize, bool& isSave) const {
  isSave = false;
  if (!path) return false;
  const char* rel = path;
  if (!strncmp(path, "save:/", 6)) {
    isSave = true;
    rel = path + 6;
  } else if (!strncmp(path, "data:/", 6)) {
    rel = path + 6;
  } else if (strchr(path, ':')) {
    return false;
  }
  if (strstr(rel, "..")) return false;
  int n;
  if (!isSave && device_ == Ps2Device::Disc) {
    // ISO9660 level 1: upper case, backslashes, ";1" version suffix.
    n = snprintf(out, outSize, "cdrom0:\\");
    for (const char* p = rel; *p && n > 0 && static_cast<unsigned>(n) + 3 < outSize; ++p) {
      char c = *p == '/' ? '\\' : *p;
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
      out[n++] = c;
    }
    if (n <= 0 || static_cast<unsigned>(n) + 3 >= outSize) return false;
    out[n++] = ';';
    out[n++] = '1';
    out[n] = 0;
    return true;
  }
  const char* prefix = (device_ == Ps2Device::Host) ? "host:" : "mass:/";
  n = snprintf(out, outSize, "%s%s", prefix, rel);
  return n > 0 && static_cast<unsigned>(n) < outSize;
}

Span<const u8> Ps2FileSystem::readFile(const char* path, Arena& arena) {
  char full[256];
  bool isSave;
  if (!resolve(path, full, sizeof(full), isSave)) {
    RLOG_ERROR("fs", "bad path %s", path ? path : "(null)");
    return {};
  }
  int fd = open(full, O_RDONLY);
  if (fd < 0) {
    RLOG_ERROR("fs", "cannot open %s", full);
    return {};
  }
  off_t n = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);
  u8* mem = n > 0 ? static_cast<u8*>(arena.allocate(static_cast<size_t>(n))) : nullptr;
  if (!mem) {
    close(fd);
    RLOG_ERROR("fs", "no arena space for %s (%d bytes)", full, static_cast<int>(n));
    return {};
  }
  size_t got = 0;
  while (got < static_cast<size_t>(n)) {
    int r = read(fd, mem + got, static_cast<size_t>(n) - got);
    if (r <= 0) break;
    got += static_cast<size_t>(r);
  }
  close(fd);
  if (got != static_cast<size_t>(n)) return {};
  return Span<const u8>{mem, got};
}

void Ps2FileSystem::writeLog(const char* text) { RLOG_INFO("game", "%s", text); }

i32 Ps2FileSystem::openStream(const char* path) {
  char full[256];
  bool isSave;
  if (!resolve(path, full, sizeof(full), isSave)) return -1;
  for (int i = 0; i < kStreamSlots; ++i) {
    if (fds_[i] >= 0) continue;
    int fd = open(full, O_RDONLY);
    if (fd < 0) return -1;
    fds_[i] = fd;
    return i + 1;
  }
  RLOG_ERROR("fs", "all %d stream slots are in use", kStreamSlots);
  return -1;
}

u32 Ps2FileSystem::readStream(i32 h, void* dst, u32 bytes) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return 0;
  int r = read(fds_[h - 1], dst, bytes);
  return r > 0 ? static_cast<u32>(r) : 0;
}

bool Ps2FileSystem::seekStream(i32 h, u32 offset) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return false;
  return lseek(fds_[h - 1], static_cast<off_t>(offset), SEEK_SET) >= 0;
}

void Ps2FileSystem::closeStream(i32 h) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return;
  close(fds_[h - 1]);
  fds_[h - 1] = -1;
}

bool Ps2FileSystem::writeFile(const char* path, const void* data, u32 bytes) {
  char full[256];
  bool isSave;
  if (!resolve(path, full, sizeof(full), isSave) || !isSave) return false;
  int fd = open(full, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return false;
  int put = bytes ? write(fd, data, bytes) : 0;
  close(fd);
  return put == static_cast<int>(bytes);
}

}  // namespace rensga
