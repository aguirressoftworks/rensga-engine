// MVP-37: emulator launching as a separate process (emulators are GPL: never linked).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rensga/pipeline/pipeline.h"

namespace rensga {
namespace pipeline {

struct LaunchSpec {
  std::filesystem::path executable;  // game ELF or EBOOT to boot directly
  std::filesystem::path workingDir;  // dev build dir (host0: root)
  std::filesystem::path configDir;   // <project>/Library/Emulators/<emulator>
  int quitAfterSeconds = 0;
  // F5 additions (all default to the old behaviour).
  bool headless = false;   // no window at all (CI): run the emulator's headless binary when it has one
  bool embed = false;      // the editor will reparent the emulator window into a docked view (no decorations, no autosize)
  bool debugger = false;   // start the emulator with its debugger on (the TargetModule decides what that means)
  std::vector<std::pair<std::string, std::string>> env;  // extra environment variables for the emulator process
  std::filesystem::path memstickDir;  // emulator save area (memory stick / memory card) to use instead of the default
  // F5.19-F5.22 additions (all optional).
  std::filesystem::path logPath;      // the launcher writes the whole session log here when the session ends (<project>/Library/Logs/last-play.log)
  int hangTimeoutSeconds = 15;        // alive but no 'rensga: frame' heartbeat for this long = Hung (0 = off); the clock starts at the first game line
  int bootTimeoutSeconds = 60;        // no game line at all within this time after start = Hung (0 = off)
};

// How an emulator session ended (or that it has not).
enum class ExitState { Running, Exited, Crashed, Hung };

class EmulatorLauncher {
 public:
  virtual ~EmulatorLauncher() = default;
  virtual bool start(const LaunchSpec& spec, EventSink sink) = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;
  virtual std::vector<std::string> drainLog() = 0;

  // ---- F5 (Emulator Play). Defaults mean "this emulator cannot do that".
  virtual bool pause() { return false; }
  virtual bool resume() { return false; }
  // Runs exactly n fixed frames and pauses again (pauses first when running).
  virtual bool step(int n) { (void)n; return false; }
  // Running while the process lives; Exited = clean exit; Crashed = killed by a signal or non-zero exit that was not a
  // stop request; Hung = alive but the game stopped logging frames for the hang timeout.
  virtual ExitState exitState() const { return running() ? ExitState::Running : ExitState::Exited; }
  // Why the session ended: exit code (128 + signal when killed by a signal), the signal (0 = none) and a one-line sentence for the
  // console ("exited normally", "crashed: killed by signal 11 (SIGSEGV)", "hung: no heartbeat for 15 s", ...). Valid after exitState() != Running.
  virtual int exitCode() const { return -1; }
  virtual int exitSignal() const { return 0; }
  virtual std::string exitDetail() const { return {}; }
  // Sends a raw dev-control command (docs/rensga-engine/emulator-launch.md: pause, resume, step N, apppause 0|1, quit, and the test-only crash / hang).
  virtual bool sendCommand(const std::string& command) { (void)command; return false; }
  // True while the game is paused through pause() / step() (not by the user inside the emulator).
  virtual bool paused() const { return false; }
  // The emulator's top-level window (X11 window id / HWND), 0 = none (headless or not started).
  virtual uint64_t nativeWindow() const { return 0; }
  // Writes the whole log of the last session to `path` (also after a crash).
  virtual bool saveLog(const std::filesystem::path& path) const { (void)path; return false; }
};

}  // namespace pipeline
}  // namespace rensga
