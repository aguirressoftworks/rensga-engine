#include "rensga/editor/profiler_model.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rensga {
namespace editor {

namespace {
bool numAfter(const char* s, const char* key, uint32_t& v) {
  const char* k = std::strstr(s, key);
  if (!k) return false;
  v = static_cast<uint32_t>(std::strtoul(k + std::strlen(key), nullptr, 10));
  return true;
}
}  // namespace

const char* profSourceName(ProfilerFeed::Source s) {
  switch (s) {
    case ProfilerFeed::Source::None: return "none";
    case ProfilerFeed::Source::Log: return "log";
    case ProfilerFeed::Source::Tcp: return "tcp";
    case ProfilerFeed::Source::Editor: return "editor";
  }
  return "?";
}

bool parseProfFrameLine(const std::string& line, ProfFrameData& out) {
  const size_t at = line.find("rensga: pf ");
  if (at == std::string::npos) return false;
  const char* s = line.c_str() + at + 11;
  ProfFrameData f;
  if (!numAfter(s, "f=", f.frame) || !numAfter(s, " t=", f.totalUs)) return false;
  numAfter(s, " u=", f.sectionUs[0]);
  numAfter(s, " r=", f.sectionUs[1]);
  numAfter(s, " p=", f.sectionUs[2]);
  numAfter(s, " a=", f.sectionUs[3]);
  numAfter(s, " d=", f.draws);
  numAfter(s, " tr=", f.triangles);
  numAfter(s, " tx=", f.textures);
  numAfter(s, " v=", f.vramBytes);
  numAfter(s, " m=", f.memUsed);
  numAfter(s, " mc=", f.memCapacity);
  if (const char* sm = std::strstr(s, " s=")) {
    std::string list(sm + 3);
    while (!list.empty() && (list.back() == '\r' || list.back() == '\n' || list.back() == ' ')) list.pop_back();
    if (list != "-") {
      size_t pos = 0;
      while (pos <= list.size()) {
        size_t e = list.find(',', pos);
        if (e == std::string::npos) e = list.size();
        const std::string item = list.substr(pos, e - pos);
        pos = e + 1;
        const size_t c1 = item.find(':');
        if (c1 == std::string::npos) continue;
        ProfSampleRec r;
        r.name = item.substr(0, c1);
        int parent = -1;
        unsigned us = 0, calls = 0;
        if (std::sscanf(item.c_str() + c1 + 1, "%u:%u:%d", &us, &calls, &parent) < 2) continue;
        r.us = us;
        r.calls = calls;
        r.parent = parent >= 0 && static_cast<size_t>(parent) < f.samples.size() ? parent : -1;
        f.samples.push_back(std::move(r));
      }
    }
  }
  out = std::move(f);
  return true;
}

ProfilerFeed& ProfilerFeed::instance() {
  static ProfilerFeed f;
  return f;
}

ProfilerFeed::~ProfilerFeed() { detach(); }

bool ProfilerFeed::feedLogLine(const std::string& line) {
  if (line.find("rensga: pf ") == std::string::npos) return false;
  ProfFrameData f;
  if (!parseProfFrameLine(line, f)) return false;
  push(std::move(f), Source::Log);
  return true;
}

void ProfilerFeed::push(ProfFrameData f, Source from) {
  std::lock_guard<std::mutex> g(mu_);
  frames_.push_back(std::move(f));
  while (frames_.size() > kMax) frames_.pop_front();
  ++total_;
  source_ = from;
}

void ProfilerFeed::clear() {
  std::lock_guard<std::mutex> g(mu_);
  frames_.clear();
  total_ = 0;
  source_ = Source::None;
}

std::vector<ProfFrameData> ProfilerFeed::snapshot() const {
  std::lock_guard<std::mutex> g(mu_);
  return std::vector<ProfFrameData>(frames_.begin(), frames_.end());
}

std::string ProfilerFeed::state() const {
  switch (state_.load()) {
    case 1: return "connecting";
    case 2: return "connected";
    case 3: return "closed";
    case 4: return "unavailable";
    default: return "idle";
  }
}

void ProfilerFeed::detach() {
  stop_ = true;
  if (th_.joinable()) th_.join();
  state_ = 0;
  port_ = 0;
}

bool ProfilerFeed::attachTcp(int port, int timeoutMs) {
#if defined(_WIN32)
  (void)port; (void)timeoutMs;
  state_ = 4;
  return false;
#else
  detach();
  stop_ = false;
  port_ = port;
  state_ = 1;
  th_ = std::thread([this, port, timeoutMs] { tcpLoop(port, timeoutMs); });
  return true;
#endif
}

void ProfilerFeed::tcpLoop(int port, int timeoutMs) {
#if !defined(_WIN32)
  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();
  int fd = -1;
  while (!stop_) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) break;
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(static_cast<uint16_t>(port));
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) break;
    close(fd);
    fd = -1;
    if (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() > timeoutMs) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  if (fd < 0) {
    state_ = 3;
    return;
  }
  state_ = 2;
  std::string buf;
  char tmp[2048];
  while (!stop_) {
    pollfd p = {fd, POLLIN, 0};
    const int r = poll(&p, 1, 100);
    if (r < 0) break;
    if (r == 0) continue;
    const ssize_t n = recv(fd, tmp, sizeof tmp, 0);
    if (n <= 0) break;
    buf.append(tmp, static_cast<size_t>(n));
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      ProfFrameData f;
      if (parseProfFrameLine(buf.substr(0, nl), f)) push(std::move(f), Source::Tcp);
      buf.erase(0, nl + 1);
    }
  }
  close(fd);
  state_ = 3;
#else
  (void)port; (void)timeoutMs;
#endif
}

}  // namespace editor
}  // namespace rensga
