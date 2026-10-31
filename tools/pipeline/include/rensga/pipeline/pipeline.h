// MVP-34: build/launch pipeline shared by the CLI and the editor. Never names a console: it finds
// target modules through TargetRegistry (editor/core).
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rensga {
namespace pipeline {

enum class ExitState;  // defined in emulator_launcher.h

enum class Stage { Validate, Cook, Compile, Package, Launch };
enum class Status { Started, Progress, Succeeded, Failed, Skipped };

struct Event {
  Stage stage = Stage::Validate;
  Status status = Status::Started;
  std::string message;
  std::string assetGuid;  // set when the event is about one asset (errors name asset + GUID)
};

struct BuildRequest {
  std::filesystem::path project;
  std::string target;
  bool release = false;
  bool launch = false;
  int quitAfterSeconds = 0;  // 0 = run until closed
  Stage lastStage = Stage::Launch;  // stop after this stage (validate/cook/build commands)
  // F5.19-F5.22 (Launch only): handed to the emulator launcher.
  bool headless = false;         // no emulator window (CI)
  bool debugger = false;         // start the emulator with its debugger on ("Relaunch with debugger")
  bool embed = false;            // the editor reparents the emulator window into a docked view
  int hangTimeoutSeconds = 15;   // no heartbeat for this long = Hung (0 = off)
};

using EventSink = std::function<void(const Event&)>;

const char* stageName(Stage s);
const char* statusName(Status s);

class Session {
 public:
  Session();
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Runs Validate -> Cook -> Compile -> Package -> Launch (up to request.lastStage; Launch only if
  // request.launch). Returns true if all stages that ran succeeded. After a successful Launch the
  // emulator keeps running: poll running() and drainGameLog().
  bool run(const BuildRequest& request, EventSink sink);
  void stop();                 // stops the launched emulator / cancels a running build
  bool running() const;
  std::vector<std::string> drainGameLog();  // lines captured since the last call

  // F5.20 / F5.22: control and state of the launched emulator session. False / defaults when nothing is launched or the emulator
  // cannot do it.
  bool pause();
  bool resume();
  bool step(int frames);
  bool paused() const;
  bool command(const std::string& raw);  // raw dev-control command (apppause 1, quit, ...)
  ExitState exitState() const;
  int exitCode() const;
  int exitSignal() const;
  std::string exitDetail() const;
  bool saveLog(const std::filesystem::path& path) const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace pipeline
}  // namespace rensga
