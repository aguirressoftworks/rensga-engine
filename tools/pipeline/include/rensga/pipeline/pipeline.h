// MVP-34: build/launch pipeline shared by the CLI and the editor. Never names a console: it finds
// target modules through TargetRegistry (editor/core).
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rensga {
namespace pipeline {

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

  // Runs the stages for the request, reporting every event. Returns true if all stages that ran
  // succeeded. Foundation stub: emits one Failed event "not implemented".
  bool run(const BuildRequest& request, EventSink sink);
  void stop();                 // stops the launched emulator / cancels a running build
  bool running() const;
  std::vector<std::string> drainGameLog();  // lines captured since the last call

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace pipeline
}  // namespace rensga
