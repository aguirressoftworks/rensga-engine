// Play/Stop: runs pipeline::Session on a worker thread. The toolbar button and --play call play().
#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
  void stop();
  bool busy() const;
  PlayStatus status() const { return status_.load(); }
  // Moves game log + pipeline events into EditorLog; call once per frame from the UI thread.
  void pump();

 private:
  void worker(pipeline::BuildRequest req);
  pipeline::Session session_;
  std::thread thread_;
  std::atomic<PlayStatus> status_{PlayStatus::Idle};
  std::atomic<bool> done_{false};
  std::mutex mu_;
  std::vector<pipeline::Event> events_;
};

}  // namespace editor
}  // namespace rensga
