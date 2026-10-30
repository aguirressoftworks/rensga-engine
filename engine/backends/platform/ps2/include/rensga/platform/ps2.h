// PS2 platform backend (EE): libpad input, EE clock, boot info (video region from the BIOS ROMVER),
// file system for cdrom0: (disc), host: (dev) and mass: (USB).
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

// What the platform learned about the machine at init. Read it through Ps2Platform::info().
struct Ps2Info {
  bool pal = false;         // true: PAL console/BIOS region (E), false: NTSC (J, A, H, C...)
  char region = '?';        // 5th character of rom0:ROMVER ('J' Japan, 'A' America, 'E' Europe, 'H' Asia, 'C' China)
  char romVer[17] = {0};    // rom0:ROMVER, e.g. "0250EC20010217"
  bool padOpen = false;     // libpad is running (SIO2MAN/PADMAN loaded)
};

class Ps2Platform : public IPlatform {
 public:
  // Init order, each step documented in the .cpp: SifInitRpc, optional IOP reset, rom0: IRX load, libpad, ROMVER.
  bool init() override;
  void shutdown() override {}
  void pollInput(VirtualGamepad& pad) override;  // DualShock 2: both sticks (analog mode), L2/R2 as buttons
  void setVibration(Scalar low, Scalar high, Scalar seconds) override;  // F3.06: small motor on/off, large motor 0-255
  double timeSeconds() override;  // EE cycle counter, accumulated to 64 bits (call at least every 10 s)
  bool shouldQuit() override { return false; }
  const Ps2Info& info() const { return info_; }

 private:
  Ps2Info info_;
  bool padInit_ = false;
  bool padModeSet_ = false;
  bool actAligned_ = false, vibDirty_ = false;
  Scalar vibLow_ = 0, vibHigh_ = 0;
  void applyVibration();
  unsigned long long ticks_ = 0;  // accumulated EE CPU cycles
  unsigned lastTick_ = 0;
  bool clockStarted_ = false;
};

// Dev (host:) vs disc vs USB. Auto probes cdrom0:\SYSTEM.CNF;1 and chooses Disc, else Host.
enum class Ps2Device { Auto, Disc, Host, Usb };

// IFileSystem contract (Engine 1): "data:/<rel>" is the game data root and "save:/<rel>" the writable
// area. Mapping: data:/ = cdrom0:\<REL>;1 (disc), host:<rel> (dev), mass:/<rel> (USB).
// save:/ = host:<rel> (dev), mc0:/RENSGA/<rel> (memory card slot 1, disc boots; flat names only) or mass:/<rel> (USB).
// Memory card access goes through libmc with the BIOS-ROM modules (XMCMAN/XMCSERV); no icon.sys is written.
// Streams use a fixed table of 8 slots, no heap. All I/O goes through newlib's POSIX calls (they sit on fio).
class Ps2FileSystem : public IFileSystem {
 public:
  static constexpr int kStreamSlots = 8;
  explicit Ps2FileSystem(Ps2Device device = Ps2Device::Auto);

  Span<const u8> readFile(const char* path, Arena& arena) override;
  void writeLog(const char* text) override;
  // Contract 3 methods, declared without `override` so this header compiles against an older interface.
  i32 openStream(const char* path) override;
  u32 readStream(i32 h, void* dst, u32 bytes) override;
  bool seekStream(i32 h, u32 offset) override;
  void closeStream(i32 h) override;
  bool writeFile(const char* path, const void* data, u32 bytes) override;

  Ps2Device device() const { return device_; }
  // Maps a virtual path to a device path. Returns false for bad paths. Exposed for tests.
  bool resolve(const char* path, char* out, unsigned outSize, bool& isSave) const;

 private:
  Ps2Device device_;
  int fds_[kStreamSlots];
  bool mcFd_[kStreamSlots];  // the slot holds a libmc handle (memory card), not a POSIX fd
};

}  // namespace rensga
