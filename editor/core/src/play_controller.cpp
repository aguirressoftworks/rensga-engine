#include "rensga/editor/play_controller.h"

#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

const char* playStatusName(PlayStatus s) {
  switch (s) {
    case PlayStatus::Idle: return "Idle";
    case PlayStatus::Building: return "Building";
    case PlayStatus::Launching: return "Launching";
    case PlayStatus::Running: return "Running";
    case PlayStatus::Stopped: return "Stopped";
  }
  return "?";
}

PlayController::PlayController() {}
PlayController::~PlayController() {
  session_.stop();
  if (thread_.joinable()) thread_.join();
}

bool PlayController::busy() const {
  PlayStatus s = status_.load();
  return s == PlayStatus::Building || s == PlayStatus::Launching || s == PlayStatus::Running;
}

bool PlayController::play(const std::filesystem::path& project, const std::string& target, int quitAfter) {
  if (busy()) return false;
  if (thread_.joinable()) thread_.join();
  pipeline::BuildRequest req;
  req.project = project;
  req.target = target;
  req.launch = true;
  req.quitAfterSeconds = quitAfter;
  done_ = false;
  status_ = PlayStatus::Building;
  EditorLog::instance().info("play: building " + target + " for " + project.string());
  thread_ = std::thread([this, req] { worker(req); });
  return true;
}

void PlayController::worker(pipeline::BuildRequest req) {
  bool ok = session_.run(req, [this](const pipeline::Event& e) {
    if (e.stage == pipeline::Stage::Launch) {
      if (e.status == pipeline::Status::Started) status_ = PlayStatus::Launching;
      else if (e.status == pipeline::Status::Succeeded || e.status == pipeline::Status::Progress)
        status_ = PlayStatus::Running;
    } else if (status_.load() == PlayStatus::Idle) {
      status_ = PlayStatus::Building;
    }
    std::lock_guard<std::mutex> g(mu_);
    events_.push_back(e);
  });
  {
    pipeline::Event e;
    e.stage = pipeline::Stage::Launch;
    e.status = ok ? pipeline::Status::Succeeded : pipeline::Status::Failed;
    e.message = ok ? "session ended" : "session failed";
    std::lock_guard<std::mutex> g(mu_);
    events_.push_back(e);
  }
  status_ = PlayStatus::Stopped;
  done_ = true;
}

void PlayController::stop() { session_.stop(); }

void PlayController::pump() {
  std::vector<pipeline::Event> ev;
  {
    std::lock_guard<std::mutex> g(mu_);
    ev.swap(events_);
  }
  EditorLog& log = EditorLog::instance();
  for (const pipeline::Event& e : ev) {
    std::string t = std::string(pipeline::stageName(e.stage)) + " " + pipeline::statusName(e.status);
    if (!e.message.empty()) t += ": " + e.message;
    if (!e.assetGuid.empty()) t += " [" + e.assetGuid + "]";
    if (e.status == pipeline::Status::Failed) log.error(t); else log.info(t);
  }
  for (const std::string& l : session_.drainGameLog()) log.game(l);
}

}  // namespace editor
}  // namespace rensga
