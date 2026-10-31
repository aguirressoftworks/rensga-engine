// Small POSIX child-process helper (host tools only). Output (stdout+stderr) is delivered line by line.
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rensga {
namespace pipeline {

using LineSink = std::function<void(const std::string&)>;

struct ProcessSpec {
  std::vector<std::string> argv;
  std::filesystem::path cwd;                                  // empty = inherit
  std::vector<std::pair<std::string, std::string>> env;       // added to the inherited environment
};

class Process {
 public:
  Process() = default;
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  bool start(const ProcessSpec& spec, LineSink onLine);
  bool running();                  // polls; false once the child exited
  int wait();                      // blocks until exit and the output is drained; exit code, or 128+signal
  void terminate(int graceMs = 2000);  // SIGTERM to the process group, SIGKILL after graceMs
  int exitSignal();                // signal number that ended the child, 0 = it exited normally (valid once running() is false)
  long pid() const { return pid_; }  // -1 before the first start
  bool signalGroup(int sig);       // kill(-pgid, sig) while the child lives (SIGSTOP / SIGCONT fallback for emulators without a control channel)

 private:
  bool poll(bool block);
  int pid_ = -1;
  int fd_ = -1;
  int exitCode_ = -1;
  int exitSignal_ = 0;
  bool done_ = true;
  std::thread reader_;
  std::mutex mu_;
};

// Runs to completion; stops the child early when *cancel becomes true. Returns exit code (-1 = could not start).
int runProcess(const ProcessSpec& spec, const LineSink& onLine, const std::atomic<bool>* cancel = nullptr);

}  // namespace pipeline
}  // namespace rensga
