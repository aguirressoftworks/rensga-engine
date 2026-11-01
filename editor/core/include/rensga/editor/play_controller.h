// Play/Stop on a console target: runs pipeline::Session on a worker thread. The toolbar button and --play call play().
// The status follows the emulator process: Building -> Launching -> Running, and Stopped only once the process has exited
// (F5.20: before, it flipped to Stopped as soon as the emulator was launched and the Stop button went dead).
#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rensga/pipeline/emulator_launcher.h"
#include "rensga/pipeline/pipeline.h"

namespace rensga {
namespace editor {

enum class PlayStatus { Idle, Building, Launching, Running, Stopped };
const char* playStatusName(PlayStatus s);

class PlayController {
 public:
  PlayController();
  ~PlayController();
  // Starts a dev build + launch of `target` on a worker thread. False if already busy.
  bool play(const std::filesystem::path& project, const std::string& target, int quitAfterSeconds = 0);
  // Same, with the extras of the last request kept (headless / debugger / hang timeout).
  bool play(const pipeline::BuildRequest& request);
  // F6.05 (Build Settings): Build only (no launch) or Build and Run of request.platform on the same worker; the status goes Building ->
  // Stopped for a build and follows the process for a run. False if busy.
  bool build(const pipeline::BuildRequest& request);
  std::filesystem::path lastOutput() const { return session_.lastOutput(); }   // EBOOT.PBP / rensga.iso / folder / APK of the last good build
  bool builtNotRun() const { return session_.builtNotRun(); }                  // built, but nothing could run it here (no device, no wine)
  std::string lastNote() const { return session_.lastNote(); }
  void stop();                       // returns at once; status becomes Stopped when the process is gone
  bool busy() const;
  PlayStatus status() const { return status_.load(); }
  bool finished() const { return done_.load(); }   // the last session has ended (the emulator process is gone)
  bool lastOk() const { return ok_.load(); }        // ... and this is its result (false: pipeline failure, crash or hang)

  // F5.20: Pause / Resume / Step of the running game. False when nothing runs or the emulator cannot do it.
  bool pause();
  bool resume();
  bool step(int frames);
  bool paused() const { return session_.paused(); }
  bool command(const std::string& raw) { return session_.command(raw); }  // raw dev-control command (tests: apppause 1, crash, hang)

  // F5.22: how the session is going or ended. Running while it lives (unless a hang was detected).
  pipeline::ExitState exitState() const { return session_.exitState(); }
  std::string exitDetail() const { return session_.exitDetail(); }
  int exitCode() const { return session_.exitCode(); }
  // "Relaunch with debugger": stops the session (if any) and plays again with the emulator's debugger on. False without a previous Play.
  bool relaunchWithDebugger();
  // Time from Play to the emulator being launched, in ms (0 = not measured yet). Also logged as 'pipeline: ready N ms after Play'.
  long lastReadyMs() const { return readyMs_.load(); }
  std::filesystem::path lastLogPath() const { return lastLog_; }

  // Moves game log + pipeline events into EditorLog; call once per frame from the UI thread.
  void pump();

 private:
  bool start(pipeline::BuildRequest req);
  void worker(pipeline::BuildRequest req);
  void attachProfiler();
  bool profAttached_ = false;
  pipeline::Session session_;
  std::thread thread_;
  std::atomic<PlayStatus> status_{PlayStatus::Idle};
  std::atomic<bool> done_{false};
  std::atomic<bool> ok_{true};
  std::atomic<long> readyMs_{0};
  std::atomic<bool> relaunch_{false};
  std::atomic<bool> stopAsked_{false};
  pipeline::BuildRequest last_;
  bool haveLast_ = false;
  std::filesystem::path lastLog_;
  std::mutex mu_;
  std::vector<pipeline::Event> events_;
};

}  // namespace editor
}  // namespace rensga
