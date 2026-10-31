// MVP-37 / F5.19-F5.22: PPSSPP as a separate process (GPL: never linked). Boots the ELF directly, keeps a per-project sandbox at
// <configDir>/ppsspp (config + memory stick), no BIOS needed. The game reads its data through host0: = LaunchSpec::workingDir.
// Captures the emulator's output plus a tail of host0:/rensga.log (duplicates between the two are dropped), sends Pause / Resume /
// Step through host0:/rensga.ctl (docs/rensga-engine/emulator-launch.md), and classifies how the session ended.
#pragma once
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "rensga/pipeline/emulator_launcher.h"
#include "rensga/pipeline/process.h"

namespace rensga {
namespace editor {

class PpssppLauncher : public pipeline::EmulatorLauncher {
 public:
  PpssppLauncher() = default;
  ~PpssppLauncher() override { stop(); }
  bool start(const pipeline::LaunchSpec& spec, pipeline::EventSink sink) override;
  void stop() override;
  bool running() const override;
  std::vector<std::string> drainLog() override;

  bool pause() override;
  bool resume() override;
  bool step(int n) override;
  bool paused() const override { return paused_.load(); }
  bool sendCommand(const std::string& c) override { return sendCtl(c); }
  pipeline::ExitState exitState() const override;
  int exitCode() const override { return exitCode_.load(); }
  int exitSignal() const override { return exitSignal_.load(); }
  std::string exitDetail() const override;
  bool saveLog(const std::filesystem::path& path) const override;

  // Emulator binary: $RENSGA_PPSSPP, the ppsspp module, third_party/emulators/ppsspp/PPSSPPSDL, then PATH.
  static std::string findExecutable();
  // The windowless binary that sits next to `gui` (or $RENSGA_PPSSPP_HEADLESS, or on PATH); empty = none.
  static std::string findHeadless(const std::string& gui);
  // The version the repo pins (third_party/emulators/ppsspp/VERSION without the packaging suffix, e.g. "1.20.4").
  static std::string pinnedVersion();
  // `<exe> --version` ("v1.20.4" -> "1.20.4"); empty when it cannot be read. Cached per executable.
  static std::string installedVersion(const std::string& exe);

 private:
  void tailLoop(std::filesystem::path logFile);
  // `source` 0 = emulator output, 1 = host0:/rensga.log tail. Pairs identical game lines from the two sources and keeps one.
  void ingest(const std::string& line, int source);
  void note(const std::string& line);  // a launcher line ("[launcher] ...")
  void flushRepeat();                  // writes the "repeated N more times" line of a folded run (mu_ must not be held)
  bool sendCtl(const std::string& command);
  void finish();                       // classifies the end of the session and saves the log
  long nowMs() const;

  mutable std::mutex mu_;   // log_, all_, detail_, pending_
  std::mutex ctl_;          // process control + ctl file
  pipeline::Process proc_;
  std::thread tail_;
  pipeline::LaunchSpec spec_;
  std::filesystem::path ctlPath_;
  std::vector<std::string> cmds_;
  unsigned seq_ = 0;
  std::chrono::steady_clock::time_point t0_;

  std::atomic<bool> stopFlag_{false};
  std::atomic<bool> tailActive_{false};
  std::atomic<bool> stopRequested_{false};  // stop() or quit-after: the process death is not a crash
  std::atomic<bool> paused_{false};
  std::atomic<bool> sigStopped_{false};
  std::atomic<bool> ackedPause_{false};
  std::atomic<long> pauseDeadlineMs_{0};
  std::atomic<long> lastBeatMs_{0};
  std::atomic<bool> sawGame_{false};
  std::atomic<bool> hung_{false};
  std::atomic<bool> crashedByLog_{false};
  std::atomic<bool> ended_{false};
  std::atomic<int> exitCode_{-1};
  std::atomic<int> exitSignal_{0};
  std::vector<std::string> log_;  // not yet drained
  std::vector<std::string> all_;  // the whole session (last-play.log)
  std::string detail_;
  std::string lastEmu_;  // last emulator error text (without timestamp): identical repeats are folded into one line
  int emuRepeat_ = 0;
  std::map<std::string, int> pending_[2];  // unmatched game lines per source
};

}  // namespace editor
}  // namespace rensga
