// F6.06 (Editor 12): the frames the Profiler window shows. A running game streams one "rensga: pf ..." line per frame (engine/runtime/
// prof_stream.h) over the log channel (consoles, emulators, wine) or a localhost TCP socket (Linux player, adb-forwarded phone); Play in
// editor pushes its own numbers. No ImGui here: the model is tested without a window.
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rensga {
namespace editor {

struct ProfSampleRec {
  std::string name;
  uint32_t us = 0, calls = 0;
  int parent = -1;  // index into the same frame's samples, -1 = top level
};

struct ProfFrameData {
  uint32_t frame = 0, totalUs = 0, sectionUs[4] = {0, 0, 0, 0};  // update, render, present, audio
  uint32_t draws = 0, triangles = 0, textures = 0, vramBytes = 0, memUsed = 0, memCapacity = 0;
  std::vector<ProfSampleRec> samples;
};

// "rensga: pf f=.. t=.. u=.. r=.. p=.. a=.. d=.. tr=.. tx=.. v=.. m=.. mc=.. s=name:us:calls:parent,..." anywhere in `line`.
bool parseProfFrameLine(const std::string& line, ProfFrameData& out);

class ProfilerFeed {
 public:
  enum class Source { None, Log, Tcp, Editor };
  static ProfilerFeed& instance();
  ~ProfilerFeed();

  // A game log line. True when it was a profiler frame line (the caller then keeps it out of the Console).
  bool feedLogLine(const std::string& line);
  void push(ProfFrameData f, Source from);
  void clear();

  uint64_t totalFrames() const { return total_.load(); }  // frames received since clear()
  Source source() const { return source_.load(); }
  std::vector<ProfFrameData> snapshot() const;            // oldest first, at most kMax

  // TCP client (POSIX only): connects to 127.0.0.1:port, retrying for `timeoutMs` (the game needs a moment to start), then reads lines on a
  // worker thread. state(): "idle" | "connecting" | "connected" | "closed" | "unavailable".
  bool attachTcp(int port, int timeoutMs);
  void detach();
  std::string state() const;
  int port() const { return port_.load(); }

  static constexpr size_t kMax = 1200;

 private:
  ProfilerFeed() = default;
  void tcpLoop(int port, int timeoutMs);
  mutable std::mutex mu_;
  std::deque<ProfFrameData> frames_;
  std::atomic<uint64_t> total_{0};
  std::atomic<Source> source_{Source::None};
  std::thread th_;
  std::atomic<bool> stop_{false};
  std::atomic<int> state_{0}, port_{0};
};

const char* profSourceName(ProfilerFeed::Source s);

}  // namespace editor
}  // namespace rensga
