// MVP-37: PPSSPP as a separate process (GPL: never linked). Boots the ELF directly, keeps a
// per-project config at <configDir>/ppsspp, no BIOS needed, captures process output plus a tail of
// rensga.log from the dev build dir (host0: root).
#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
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

  // Emulator binary: $RENSGA_PPSSPP, third_party/emulators/ppsspp/PPSSPPSDL, then PATH.
  static std::string findExecutable();

 private:
  void tailLoop(std::filesystem::path logFile, int quitAfterSeconds);
  void push(const std::string& line);

  mutable std::mutex mu_;
  std::mutex ctl_;
  pipeline::Process proc_;
  std::thread tail_;
  std::atomic<bool> stopFlag_{false};
  std::atomic<bool> tailActive_{false};
  std::vector<std::string> log_;
};

}  // namespace editor
}  // namespace rensga
