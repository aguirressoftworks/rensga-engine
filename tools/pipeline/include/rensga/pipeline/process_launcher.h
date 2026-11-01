// F6.05: runs a built game (the Linux exe, the Windows exe under wine, an emulator script) as a separate process behind the EmulatorLauncher interface,
// so PlayController, the CLI and the Console treat every Build and Run target the same way. Never names a console.
#pragma once
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "rensga/pipeline/emulator_launcher.h"
#include "rensga/pipeline/process.h"

namespace rensga {
namespace pipeline {

class ProcessLauncher : public EmulatorLauncher {
 public:
  struct Config {
    std::vector<std::string> argv;                               // program + arguments
    std::filesystem::path cwd;
    std::vector<std::pair<std::string, std::string>> env;
    std::string name;                                            // for messages: "linux player", "wine", ...
    bool heartbeat = true;                                       // 'rensga: frame' lines expected: Hung when they stop (LaunchSpec timeouts)
    std::filesystem::path logFile;                               // also scraped after the process ends (an emulator script writes it)
    std::filesystem::path stopFile;                              // reserved
  };
  explicit ProcessLauncher(Config c) : cfg_(std::move(c)) {}
  ~ProcessLauncher() override { stop(); }

  bool start(const LaunchSpec& spec, EventSink sink) override;
  void stop() override;
  bool running() const override;
  std::vector<std::string> drainLog() override;
  bool pause() override;
  bool resume() override;
  ExitState exitState() const override;
  int exitCode() const override { if (started_ && !running()) {} return code_; }
  int exitSignal() const override { if (started_ && !running()) {} return sig_; }
  std::string exitDetail() const override;
  bool paused() const override { return paused_; }
  bool saveLog(const std::filesystem::path& path) const override;

 private:
  void settle() const;  // reads the exit state once the child is gone
  Config cfg_;
  mutable Process proc_;
  mutable std::mutex mu_, settleMu_;
  mutable std::deque<std::string> pending_;
  std::vector<std::string> all_;
  std::atomic<bool> stopAsked_{false}, paused_{false};
  mutable bool settled_ = false;  // under settleMu_
  mutable int code_ = -1, sig_ = 0;
  bool started_ = false;
  int hangSec_ = 15, bootSec_ = 60;
  std::chrono::steady_clock::time_point t0_, lastLine_;
  std::atomic<bool> sawGame_{false};
};

}  // namespace pipeline
}  // namespace rensga
