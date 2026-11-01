#include "rensga/pipeline/process.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace rensga {
namespace pipeline {

namespace {
long long nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}
}  // namespace

Process::~Process() {
  terminate(500);
  if (reader_.joinable()) reader_.join();
  if (fd_ >= 0) close(fd_);
}

bool Process::start(const ProcessSpec& spec, LineSink onLine) {
  if (spec.argv.empty() || !done_) return false;
  if (reader_.joinable()) reader_.join();
  int p[2];
  if (pipe(p) != 0) return false;
  pid_t pid = fork();
  if (pid < 0) { close(p[0]); close(p[1]); return false; }
  if (pid == 0) {
    setpgid(0, 0);
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[0]);
    close(p[1]);
    int nul = open("/dev/null", O_RDONLY);
    if (nul >= 0) { dup2(nul, 0); close(nul); }
    // F6.05: nothing but stdio goes to the child. A daemon the child starts (wineserver) would otherwise keep the build lock (flock) and
    // other descriptors of the editor open after the game ended.
    for (int fd = 3; fd < 1024; ++fd) close(fd);
    if (!spec.cwd.empty() && chdir(spec.cwd.c_str()) != 0) _exit(127);
    for (auto& kv : spec.env) setenv(kv.first.c_str(), kv.second.c_str(), 1);
    std::vector<char*> args;
    for (auto& a : spec.argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  setpgid(pid, pid);
  close(p[1]);
  pid_ = pid;
  fd_ = p[0];
  done_ = false;
  exitCode_ = -1;
  exitSignal_ = 0;
  int fd = fd_;
  exitedAtMs_ = 0;
  reader_ = std::thread([this, fd, onLine]() {
    std::string buf;
    char tmp[4096];
    for (;;) {
      // A daemon the child started (wineserver) can keep the pipe open long after the child ended: stop shortly after the child was reaped.
      pollfd pfd = {fd, POLLIN, 0};
      const int pr = ::poll(&pfd, 1, 100);
      if (pr == 0) {
        const long long at = exitedAtMs_.load();
        if (at && nowMs() - at > 300) break;
        continue;
      }
      if (pr < 0) break;
      ssize_t n = read(fd, tmp, sizeof tmp);
      if (n <= 0) break;
      for (ssize_t i = 0; i < n; ++i) {
        if (tmp[i] == '\n') {
          if (!buf.empty() && buf.back() == '\r') buf.pop_back();
          if (onLine) onLine(buf);
          buf.clear();
        } else {
          buf.push_back(tmp[i]);
        }
      }
    }
    if (!buf.empty() && onLine) onLine(buf);
  });
  return true;
}

bool Process::poll(bool block) {
  std::lock_guard<std::mutex> lock(mu_);
  if (done_) return false;
  int status = 0;
  pid_t r = waitpid(pid_, &status, block ? 0 : WNOHANG);
  if (r == 0) return true;
  if (r < 0) {
    waitErrno_ = errno;
    exitCode_ = -1;
  } else if (WIFEXITED(status)) {
    exitCode_ = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    exitCode_ = 128 + WTERMSIG(status);
    exitSignal_ = WTERMSIG(status);
  }
  done_ = true;
  exitedAtMs_ = nowMs();
  return false;
}

bool Process::running() { return poll(false); }

int Process::exitSignal() {
  std::lock_guard<std::mutex> lock(mu_);
  return exitSignal_;
}

bool Process::signalGroup(int sig) {
  if (!running()) return false;
  return killpg(pid_, sig) == 0;
}

int Process::wait() {
  poll(true);
  if (reader_.joinable()) reader_.join();
  if (fd_ >= 0) { close(fd_); fd_ = -1; }
  return exitCode_;
}

void Process::terminate(int graceMs) {
  if (!running()) return;
  killpg(pid_, SIGTERM);
  auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
  while (running() && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  if (running()) killpg(pid_, SIGKILL);
  wait();
}

int runProcess(const ProcessSpec& spec, const LineSink& onLine, const std::atomic<bool>* cancel) {
  Process p;
  if (!p.start(spec, onLine)) return -1;
  while (p.running()) {
    if (cancel && cancel->load()) { p.terminate(); return -1; }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  return p.wait();
}

}  // namespace pipeline
}  // namespace rensga
