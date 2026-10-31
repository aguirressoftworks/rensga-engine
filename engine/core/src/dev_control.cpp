// F5.19 / F5.20: dev control channel. The editor's launcher writes commands to "host0:/rensga.ctl" (PSP) or to the file named by the
// RENSGA_DEVCTL environment variable (desktop player, headless sim); devControlPoll() reads them once per loop iteration.
//
// File format (text, rewritten whole by the launcher through a temp file + rename, so a read never sees half a file):
//   one command per line: "<seq> <command> [argument]", seq strictly increasing from 1. Lines with seq <= the last applied one are
//   ignored, so re-reading the same file is harmless. Commands:
//     pause | resume | step <N> | apppause <0|1> | quit        (the editor's Pause / Resume / Step / Home popup test / Stop)
//     crash | hang                                              (test only: the pipeline tests use them to prove crash / hang detection)
// Log lines (read by the launcher and the tests):
//     rensga: devctl paused frame=<f>   |   rensga: devctl resumed frame=<f>
//     rensga: devctl step <n> frame=<f before>   then   rensga: devctl stepped <n> frame=<f after>
//     rensga: devctl apppause <0|1>     |   rensga: devctl quit
// No heap allocation: raw open/read on a 512-byte buffer.
#include "rensga/core/dev_control.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rensga/core/app_events.h"
#include "rensga/core/application.h"
#include "rensga/core/log.h"
#include "rensga/core/time.h"

namespace rensga {

namespace {

struct DevCtl {
  bool init = false;
  const char* path = nullptr;  // null = channel off (not a PSP and RENSGA_DEVCTL unset)
  u32 lastSeq = 0;
  bool paused = false;
  u32 pendingStep = 0;   // frames to hand out at the next poll
  u32 stepReport = 0;    // a finished step to report at the next poll
  u32 polls = 0;
  u32 fails = 0;         // consecutive failed opens: the poll interval backs off, so a release build without host0: costs nothing
};
DevCtl g;

u32 interval() {
  if (g.paused || g.pendingStep) return 1;
  return g.fails > 8 ? 120 : 8;  // running: look every 8 iterations (~130 ms at 60 fps)
}

void apply(const char* cmd, const char* arg) {
  const unsigned long long f = static_cast<unsigned long long>(Time::frameCount());
  if (strcmp(cmd, "pause") == 0) {
    g.paused = true;
    g.pendingStep = 0;
    RLOG_INFO("game", "rensga: devctl paused frame=%llu", f);
  } else if (strcmp(cmd, "resume") == 0) {
    g.paused = false;
    g.pendingStep = 0;
    RLOG_INFO("game", "rensga: devctl resumed frame=%llu", f);
  } else if (strcmp(cmd, "step") == 0) {
    int n = arg ? atoi(arg) : 1;
    if (n < 1) n = 1;
    if (n > 600) n = 600;
    g.paused = true;
    g.pendingStep = static_cast<u32>(n);
    RLOG_INFO("game", "rensga: devctl step %d frame=%llu", n, f);
  } else if (strcmp(cmd, "apppause") == 0) {
    const bool v = arg && atoi(arg) != 0;
    injectApplicationPause(v);
    RLOG_INFO("game", "rensga: devctl apppause %d", v ? 1 : 0);
  } else if (strcmp(cmd, "quit") == 0) {
    RLOG_INFO("game", "rensga: devctl quit");
    Application::Quit();
  } else if (strcmp(cmd, "crash") == 0) {
    RLOG_ERROR("game", "rensga: devctl crash (test)");
    log::flushAll();
    volatile int* bad = nullptr;
    *bad = 1;
  } else if (strcmp(cmd, "hang") == 0) {
    RLOG_ERROR("game", "rensga: devctl hang (test)");
    log::flushAll();
    for (;;) {
    }
  } else {
    RLOG_WARN("game", "rensga: devctl unknown command '%s'", cmd);
  }
}

void readCommands() {
  const int fd = ::open(g.path, O_RDONLY);
  if (fd < 0) {
    if (g.fails < 1000000) ++g.fails;
    return;
  }
  g.fails = 0;
  char buf[512];
  const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
  ::close(fd);
  if (n <= 0) return;
  buf[n] = 0;
  for (char* p = buf; *p;) {
    char* line = p;
    while (*p && *p != '\n') ++p;
    if (*p) *p++ = 0;
    char* e = line + strlen(line);
    while (e > line && (e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
    if (!*line) continue;
    char* sp = nullptr;
    const unsigned long seq = strtoul(line, &sp, 10);
    if (sp == line || seq <= g.lastSeq) continue;
    while (*sp == ' ') ++sp;
    char* arg = sp;
    while (*arg && *arg != ' ') ++arg;
    if (*arg) *arg++ = 0;
    g.lastSeq = static_cast<u32>(seq);
    apply(sp, *arg ? arg : nullptr);
  }
}

}  // namespace

u32 devControlPoll() {
  if (!g.init) {
    g.init = true;
    if (const char* e = getenv("RENSGA_DEVCTL")) {
      if (*e) g.path = e;
    }
#if defined(__PSP__) || defined(_PSP)
    if (!g.path) g.path = "host0:/rensga.ctl";
#endif
  }
  if (!g.path) return 1;
  if (g.stepReport) {  // the frames of the last step ran between the two polls
    RLOG_INFO("game", "rensga: devctl stepped %u frame=%llu", static_cast<unsigned>(g.stepReport), static_cast<unsigned long long>(Time::frameCount()));
    g.stepReport = 0;
  }
  if (g.polls++ % interval() == 0) readCommands();
  if (g.pendingStep) {
    const u32 n = g.pendingStep;
    g.pendingStep = 0;
    g.stepReport = n;
    return n;
  }
  return g.paused ? 0 : 1;
}

}  // namespace rensga
