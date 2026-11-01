#include "prof_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/core/log.h"

#if defined(RENSGA_PROF_SOCKET) && !defined(_WIN32)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rensga {

namespace {
void cleanName(const char* in, char* out, u32 cap) {
  u32 i = 0;
  for (; in && in[i] && i + 1 < cap; ++i) {
    const char c = in[i];
    out[i] = (c == ' ' || c == ',' || c == ':' || c == '=' || c == '\n' || c == '\r') ? '_' : c;
  }
  out[i] = 0;
  if (i == 0) { out[0] = '_'; out[1] = 0; }
}
}  // namespace

u32 profFormatFrame(const ProfFrameRecord& r, char* buf, u32 cap) {
  if (!cap) return 0;
  int n = snprintf(buf, cap, "rensga: pf f=%u t=%u u=%u r=%u p=%u a=%u d=%u tr=%u tx=%u v=%u m=%u mc=%u s=", r.frame, r.totalUs, r.sectionUs[0],
                   r.sectionUs[1], r.sectionUs[2], r.sectionUs[3], r.draws, r.triangles, r.textures, r.vramBytes, r.memUsed, r.memCapacity);
  if (n < 0) { buf[0] = 0; return 0; }
  u32 len = static_cast<u32>(n) < cap ? static_cast<u32>(n) : cap - 1;
  if (r.sampleCount == 0) {
    if (len + 1 < cap) { buf[len++] = '-'; buf[len] = 0; }
    return len;
  }
  for (u32 i = 0; i < r.sampleCount && i < kProfStreamMaxSamples; ++i) {
    char nm[24];
    cleanName(r.samples[i].name, nm, sizeof nm);
    n = snprintf(buf + len, cap - len, "%s%s:%u:%u:%d", i ? "," : "", nm, r.samples[i].us, r.samples[i].calls, static_cast<int>(r.samples[i].parent));
    if (n < 0 || static_cast<u32>(n) >= cap - len) break;
    len += static_cast<u32>(n);
  }
  return len;
}

void ProfStream::init() {
  if (inited_) return;
  inited_ = true;
#if defined(RENSGA_PROF_STREAM_LOG)
  log_ = true;
#endif
  const char* mode = getenv("RENSGA_PROF_STREAM");
  if (!mode || !*mode) return;
  bool wantTcp = false;
  int port = kProfStreamDefaultPort;
  if (!strncmp(mode, "log", 3)) log_ = true;
  else if (!strncmp(mode, "tcp", 3) || !strncmp(mode, "both", 4)) {
    wantTcp = true;
    if (!strncmp(mode, "both", 4)) log_ = true;
    const char* c = strchr(mode, ':');
    if (c && atoi(c + 1) > 0) port = atoi(c + 1);
  }
  if (!wantTcp) return;
#if defined(RENSGA_PROF_SOCKET) && !defined(_WIN32)
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(static_cast<u16>(port));
  if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(fd, 1) != 0) {
    RLOG_WARN("game", "rensga: prof stream cannot listen on 127.0.0.1:%d", port);
    close(fd);
    return;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  listen_ = fd;
  port_ = port;
  RLOG_INFO("game", "rensga: prof stream listening 127.0.0.1:%d", port);
#else
  (void)port;
  RLOG_WARN("game", "rensga: prof stream tcp is not available in this build (use RENSGA_PROF_STREAM=log)");
#endif
}

void ProfStream::shutdown() {
#if defined(RENSGA_PROF_SOCKET) && !defined(_WIN32)
  if (client_ >= 0) close(client_);
  if (listen_ >= 0) close(listen_);
#endif
  client_ = listen_ = -1;
}

void ProfStream::accept() {
#if defined(RENSGA_PROF_SOCKET) && !defined(_WIN32)
  if (listen_ < 0 || client_ >= 0) return;
  const int c = ::accept(listen_, nullptr, nullptr);
  if (c < 0) return;
  fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK);
  int nd = 1;
  setsockopt(c, IPPROTO_TCP, 1 /*TCP_NODELAY*/, &nd, sizeof nd);
  client_ = c;
  tail_ = head_;  // a new client starts at the next frame
  sentInLine_ = 0;
  RLOG_INFO("game", "rensga: prof stream client connected");
#endif
}

void ProfStream::flush() {
#if defined(RENSGA_PROF_SOCKET) && !defined(_WIN32)
  while (client_ >= 0 && tail_ != head_) {
    const u32 slot = tail_ % kRing;
    const u32 len = ringLen_[slot];
    const ssize_t n = send(client_, ring_[slot] + sentInLine_, len - sentInLine_, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      close(client_);
      client_ = -1;
      return;
    }
    sentInLine_ += static_cast<u32>(n);
    if (sentInLine_ >= len) { ++tail_; sentInLine_ = 0; }
  }
#endif
}

void ProfStream::push(const ProfFrameRecord& r) {
  if (!active()) return;
  char line[kLine];
  const u32 len = profFormatFrame(r, line, sizeof line - 1);
  if (log_) RLOG_INFO("game", "%s", line);
  if (listen_ < 0) return;
  accept();
  if (client_ < 0) return;
  if (head_ - tail_ >= kRing) {  // the client is slow: drop the oldest whole line (never one half sent)
    if (sentInLine_ == 0) { ++tail_; ++dropped_; }
    else return;
  }
  const u32 slot = head_ % kRing;
  memcpy(ring_[slot], line, len);
  ring_[slot][len] = '\n';
  ringLen_[slot] = static_cast<u16>(len + 1);
  ++head_;
  flush();
}

}  // namespace rensga
