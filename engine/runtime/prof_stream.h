// F6.06 remote profiler transport (dev builds). One text line per frame, the same line on every transport:
//   rensga: pf f=<frame> t=<total_us> u=<update_us> r=<render_us> p=<present_us> a=<audio_us> d=<draws> tr=<tris> tx=<textures>
//             v=<vram_bytes> m=<arena_used> mc=<arena_capacity> s=<name>:<us>:<calls>:<parent>[,<name>:...]
// (one physical line; `s=-` when no sample ran; sample names have ' ', ',' and ':' replaced by '_'; parent = index into this line's list, -1 = top).
// Transports: the log channel (consoles and emulators: the editor reads the game log) and a localhost TCP server (PC, Android: the editor
// connects, `adb forward` for a phone). Core only: no heap, fixed ring, non-blocking sockets. Off unless asked:
//   compile define RENSGA_PROF_STREAM_LOG=1      -> log channel from the first frame (the console dev builds of the pipeline)
//   env RENSGA_PROF_STREAM=log|tcp[:port]|both    -> PC players (default port 47017)
#pragma once
#include "rensga/core/types.h"

namespace rensga {

constexpr u32 kProfStreamMaxSamples = 8;
constexpr u16 kProfStreamDefaultPort = 47017;

struct ProfFrameRecord {
  u32 frame = 0, totalUs = 0, sectionUs[4] = {};
  u32 draws = 0, triangles = 0, textures = 0, vramBytes = 0, memUsed = 0, memCapacity = 0;
  u32 sampleCount = 0;
  struct Sample {
    const char* name = "";
    u32 us = 0, calls = 0;
    i32 parent = -1;
  } samples[kProfStreamMaxSamples];
};

// Formats `r` as the line above into buf (always NUL-terminated, no trailing newline); returns the length.
u32 profFormatFrame(const ProfFrameRecord& r, char* buf, u32 cap);

class ProfStream {
 public:
  // Reads the compile define / environment (see above). Safe to call twice.
  void init();
  void shutdown();
  bool active() const { return log_ || listen_ >= 0; }
  bool logging() const { return log_; }
  int port() const { return port_; }
  bool connected() const { return client_ >= 0; }
  // One finished frame: logs it and/or queues it for the TCP client, accepts a pending connection, flushes what the socket takes.
  void push(const ProfFrameRecord& r);

 private:
  void accept();
  void flush();
  static constexpr u32 kLine = 384, kRing = 64;
  bool log_ = false, inited_ = false;
  int listen_ = -1, client_ = -1, port_ = 0;
  char ring_[kRing][kLine] = {};
  u16 ringLen_[kRing] = {};
  u32 head_ = 0, tail_ = 0;  // tail = oldest unsent; head = next write
  u32 sentInLine_ = 0;
  u32 dropped_ = 0;
};

}  // namespace rensga
