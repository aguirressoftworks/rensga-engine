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
#include "rensga/platform/ps2_icon.h"

#include <fcntl.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <libmc.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>
#include <unistd.h>

#include "rensga/core/input.h"
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
  h.l2 = (b & PAD_L2) != 0;
  h.r2 = (b & PAD_R2) != 0;
  pad.l2Value = h.l2 ? 1.0f : 0.0f;  // digital here; DualShock 2 pressure values are not used
  pad.r2Value = h.r2 ? 1.0f : 0.0f;
  if ((d.mode >> 4) == 7) {  // DualShock analog mode
    pad.leftX = Input::stickFromByte(d.ljoy_h);  // raw: Input applies the dead zone
    pad.leftY = -Input::stickFromByte(d.ljoy_v);  // stick Y grows downward; the pad's +Y is up
    pad.rightX = Input::stickFromByte(d.rjoy_h);
    pad.rightY = -Input::stickFromByte(d.rjoy_v);
  }
  applyVibration();
}

// DualShock 2 actuators: motor 0 is the small one (on/off), motor 1 the large one (0..255). Input's `high` is the light
// motor and `low` the heavy one. Sent from pollInput so libpad is only touched from the polling thread, once the pad
// is stable.
void Ps2Platform::setVibration(Scalar low, Scalar high, Scalar) {
  vibLow_ = low;
  vibHigh_ = high;
  vibDirty_ = true;
}

void Ps2Platform::applyVibration() {
  if (!vibDirty_) return;
  static char align[6] = {0, 1, static_cast<char>(0xff), static_cast<char>(0xff), static_cast<char>(0xff), static_cast<char>(0xff)};
  static char act[6] = {0, 0, 0, 0, 0, 0};
  if (!actAligned_) {
    padSetActAlign(0, 0, align);  // motor 0 -> byte 0, motor 1 -> byte 1
    actAligned_ = true;
  }
  act[0] = vibHigh_ > 0 ? 1 : 0;
  act[1] = static_cast<char>(vibLow_ <= 0 ? 0 : (vibLow_ >= 1 ? 255 : static_cast<int>(vibLow_ * 255.0f)));
  padSetActDirect(0, 0, act);
  vibDirty_ = false;
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

// ---- memory card (save:/ on disc boots) ---------------------------------------------------------------------
// Files live in mc0:/RENSGA/<name>. The modules come from the BIOS ROM (XMCMAN/XMCSERV, else MCMAN/MCSERV), loaded on
// first use. All libmc calls are asynchronous; mcSync(0, ...) blocks until each one finished. The folder gets an icon.sys and
// a small 3D icon (ps2_icon.h) the first time it is created, so the BIOS browser shows "Rensga Engine" with a gold cube.
static bool g_cardTried = false, g_cardReady = false;
// mcserv takes the IOP's fio open flags, which differ from newlib's O_* values (newlib O_WRONLY == fio read-only).
constexpr int kMcRead = 0x0001, kMcWrite = 0x0002, kMcCreate = 0x0200;

// Writes `data` to a card file unless it already exists (the player's copy of the icon is never rewritten).
static bool cardWriteOnce(const char* path, const unsigned char* data, unsigned bytes) {
  int fd = -1, r = 0;
  mcOpen(0, 0, path, kMcRead);
  mcSync(0, nullptr, &fd);
  if (fd >= 0) {
    mcClose(fd);
    mcSync(0, nullptr, &r);
    return true;
  }
  mcOpen(0, 0, path, kMcWrite | kMcCreate);
  mcSync(0, nullptr, &fd);
  if (fd < 0) return false;
  int put = 0;
  mcWrite(fd, data, static_cast<int>(bytes));
  mcSync(0, nullptr, &put);
  mcClose(fd);
  mcSync(0, nullptr, &r);
  return put == static_cast<int>(bytes);
}

alignas(64) static unsigned char g_iconSys[ps2icon::kIconSysSize + 60];
alignas(64) static unsigned char g_iconFile[ps2icon::kIconFileSize + 60];

static void cardWriteIcon() {
  if (!ps2icon::buildIconSys(g_iconSys, "Rensga Engine", " Platformer 2D", "rensga.ico")) return;
  ps2icon::buildIcon(g_iconFile);
  const bool a = cardWriteOnce("/RENSGA/rensga.ico", g_iconFile, ps2icon::kIconFileSize);
  const bool b = cardWriteOnce("/RENSGA/icon.sys", g_iconSys, ps2icon::kIconSysSize);
  RLOG_INFO("fs", "memory card icon: icon.sys %s, rensga.ico %s", b ? "ok" : "FAILED", a ? "ok" : "FAILED");
}

static bool cardInit() {
  if (g_cardTried) return g_cardReady;
  g_cardTried = true;
  int type = MC_TYPE_XMC;
  int a = SifLoadModule("rom0:XMCMAN", 0, nullptr);
  int b = a >= 0 ? SifLoadModule("rom0:XMCSERV", 0, nullptr) : -1;
  if (a < 0 || b < 0) {
    type = MC_TYPE_MC;
    a = SifLoadModule("rom0:MCMAN", 0, nullptr);
    b = a >= 0 ? SifLoadModule("rom0:MCSERV", 0, nullptr) : -1;
  }
  if (a < 0 || b < 0 || mcInit(type) < 0) {
    RLOG_WARN("fs", "memory card modules not available (mcman=%d mcserv=%d)", a, b);
    return false;
  }
  int ctype = 0, freeKb = 0, format = 0, result = 0;
  for (int i = 0; i < 2; ++i) {  // the first call after boot reports "card changed"; the second one settles
    mcGetInfo(0, 0, &ctype, &freeKb, &format);
    mcSync(0, nullptr, &result);
  }
  if (result < -1 || ctype != sceMcTypePS2) {
    RLOG_WARN("fs", "no usable PS2 memory card in slot 1 (type=%d result=%d)", ctype, result);
    return false;
  }
  if (!format) {
#ifdef RENSGA_PS2_FORMAT_BLANK_CARD
    // Emulator sandboxes start with a blank card. Formatting is only compiled in for those test builds: a game never
    // formats a player's card (an unformatted card is formatted from the BIOS browser).
    mcFormat(0, 0);
    mcSync(0, nullptr, &result);
    mcGetInfo(0, 0, &ctype, &freeKb, &format);
    mcSync(0, nullptr, &result);
    RLOG_INFO("fs", "formatted a blank memory card (format=%d free=%d KB)", format, freeKb);
#else
    RLOG_WARN("fs", "the memory card in slot 1 is not formatted; saves are off (format it in the BIOS browser)");
    return false;
#endif
  }
  mcMkDir(0, 0, "RENSGA");
  mcSync(0, nullptr, &result);  // 0 = created, -4 = exists
  g_cardReady = true;
  cardWriteIcon();
  RLOG_INFO("fs", "memory card ready (%d KB free), saves go to mc0:/RENSGA", freeKb);
  return true;
}

static bool cardPath(const char* path, char* out, unsigned outSize) {
  if (!path || strncmp(path, "save:/", 6) != 0 || strstr(path, "..") || strchr(path + 6, '/')) return false;
  int n = snprintf(out, outSize, "/RENSGA/%s", path + 6);
  return n > 0 && static_cast<unsigned>(n) < outSize;
}

Ps2FileSystem::Ps2FileSystem(Ps2Device device) : device_(device) {
  for (int& fd : fds_) fd = -1;
  for (bool& m : mcFd_) m = false;
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
  if (isSave && device_ == Ps2Device::Disc) {  // memory card
    if (!cardInit() || !cardPath(path, full, sizeof(full))) return -1;
    for (int i = 0; i < kStreamSlots; ++i) {
      if (fds_[i] >= 0) continue;
      int fd = -1;
      mcOpen(0, 0, full, kMcRead);
      mcSync(0, nullptr, &fd);
      if (fd < 0) {
        if (fd != -4) RLOG_WARN("fs", "mcOpen %s failed (%d)", full, fd);  // -4: no such file, normal for a first run
        return -1;
      }
      fds_[i] = fd;
      mcFd_[i] = true;
      return i + 1;
    }
    return -1;
  }
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
  if (mcFd_[h - 1]) {
    int got = 0;
    mcRead(fds_[h - 1], dst, static_cast<int>(bytes));
    mcSync(0, nullptr, &got);
    if (got <= 0) RLOG_WARN("fs", "mcRead returned %d", got);
    return got > 0 ? static_cast<u32>(got) : 0;
  }
  int r = read(fds_[h - 1], dst, bytes);
  return r > 0 ? static_cast<u32>(r) : 0;
}

bool Ps2FileSystem::seekStream(i32 h, u32 offset) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return false;
  if (mcFd_[h - 1]) {
    int pos = -1;
    mcSeek(fds_[h - 1], static_cast<int>(offset), SEEK_SET);
    mcSync(0, nullptr, &pos);
    return pos >= 0;
  }
  return lseek(fds_[h - 1], static_cast<off_t>(offset), SEEK_SET) >= 0;
}

void Ps2FileSystem::closeStream(i32 h) {
  if (h < 1 || h > kStreamSlots || fds_[h - 1] < 0) return;
  if (mcFd_[h - 1]) {
    int r = 0;
    mcClose(fds_[h - 1]);
    mcSync(0, nullptr, &r);
    mcFd_[h - 1] = false;
  } else {
    close(fds_[h - 1]);
  }
  fds_[h - 1] = -1;
}

bool Ps2FileSystem::writeFile(const char* path, const void* data, u32 bytes) {
  char full[256];
  bool isSave;
  if (!resolve(path, full, sizeof(full), isSave) || !isSave) return false;
  if (device_ == Ps2Device::Disc) {  // memory card: replace = delete, then create and write
    if (!cardInit() || !cardPath(path, full, sizeof(full))) return false;
    int r = 0, fd = -1;
    mcDelete(0, 0, full);
    mcSync(0, nullptr, &r);  // -4 when it did not exist
    mcOpen(0, 0, full, kMcWrite | kMcCreate);
    mcSync(0, nullptr, &fd);
    if (fd < 0) return false;
    int put = 0;
    if (bytes) {
      mcWrite(fd, data, static_cast<int>(bytes));
      mcSync(0, nullptr, &put);
    }
    mcClose(fd);
    mcSync(0, nullptr, &r);
    return put == static_cast<int>(bytes);
  }
  int fd = open(full, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return false;
  int put = bytes ? write(fd, data, bytes) : 0;
  close(fd);
  return put == static_cast<int>(bytes);
}

}  // namespace rensga
