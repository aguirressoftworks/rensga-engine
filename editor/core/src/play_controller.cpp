#include "rensga/editor/play_controller.h"

#include <chrono>
#include <cstdlib>

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
  pipeline::BuildRequest req;
  req.project = project;
  req.target = target;
  req.launch = true;
  req.quitAfterSeconds = quitAfter;
  if (const char* h = std::getenv("RENSGA_HANG_TIMEOUT")) req.hangTimeoutSeconds = std::atoi(h);  // seconds, 0 = off (tests)
  if (const char* h = std::getenv("RENSGA_PLAY_HEADLESS")) req.headless = *h && *h != '0';
  return play(req);
}

bool PlayController::play(const pipeline::BuildRequest& request) {
  if (busy()) return false;
  if (thread_.joinable()) thread_.join();
  pipeline::BuildRequest req = request;
  req.launch = true;
  req.release = false;
  last_ = req;
  haveLast_ = true;
  done_ = false;
  ok_ = true;
  readyMs_ = 0;
  stopAsked_ = false;
  status_ = PlayStatus::Building;
  EditorLog::instance().info("play: building " + req.target + " for " + req.project.string() + (req.debugger ? " (emulator debugger on)" : ""));
  thread_ = std::thread([this, req] { worker(req); });
  return true;
}

void PlayController::worker(pipeline::BuildRequest req) {
  const auto t0 = std::chrono::steady_clock::now();
  bool ok = session_.run(req, [this](const pipeline::Event& e) {
    if (e.stage == pipeline::Stage::Launch) {
      if (e.status == pipeline::Status::Started) status_ = PlayStatus::Launching;
      else if (e.status == pipeline::Status::Succeeded) status_ = PlayStatus::Running;
    } else if (status_.load() == PlayStatus::Idle) {
      status_ = PlayStatus::Building;
    }
    std::lock_guard<std::mutex> g(mu_);
    events_.push_back(e);
  });
  readyMs_ = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
  // The emulator keeps running after run() returned: stay Running until the process is gone, and report how it ended.
  bool reportedBad = false;
  if (ok && req.launch) {
    status_ = PlayStatus::Running;
    while (session_.running()) {
      const pipeline::ExitState st = session_.exitState();
      if (!reportedBad && (st == pipeline::ExitState::Hung || st == pipeline::ExitState::Crashed)) {
        reportedBad = true;
        pipeline::Event e;
        e.stage = pipeline::Stage::Launch;
        e.status = pipeline::Status::Failed;
        e.message = session_.exitDetail();
        std::lock_guard<std::mutex> g(mu_);
        events_.push_back(e);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const pipeline::ExitState st = session_.exitState();
    const bool bad = st == pipeline::ExitState::Crashed || st == pipeline::ExitState::Hung;
    pipeline::Event e;
    e.stage = pipeline::Stage::Launch;
    std::string detail = session_.exitDetail();
    e.status = bad ? pipeline::Status::Failed : pipeline::Status::Succeeded;
    e.message = "session ended: " + (detail.empty() ? std::string("exited") : detail) +
                (session_.exitSignal() ? "" : " (exit code " + std::to_string(session_.exitCode()) + ")");
    if (bad && !reportedBad) { std::lock_guard<std::mutex> g(mu_); events_.push_back(e); }
    else if (!bad) { std::lock_guard<std::mutex> g(mu_); events_.push_back(e); }
    ok = !bad;
    lastLog_ = req.project / "Library" / "Logs" / "last-play.log";
  } else {
    pipeline::Event e;
    e.stage = pipeline::Stage::Launch;
    e.status = ok ? pipeline::Status::Succeeded : pipeline::Status::Failed;
    e.message = ok ? "session ended" : "session failed";
    std::lock_guard<std::mutex> g(mu_);
    events_.push_back(e);
  }
  ok_ = ok;
  status_ = PlayStatus::Stopped;
  done_ = true;
}

void PlayController::stop() {
  stopAsked_ = true;
  session_.stop();
}

bool PlayController::pause() { return status_.load() == PlayStatus::Running && session_.pause(); }
bool PlayController::resume() { return status_.load() == PlayStatus::Running && session_.resume(); }
bool PlayController::step(int frames) { return status_.load() == PlayStatus::Running && session_.step(frames); }

bool PlayController::relaunchWithDebugger() {
  if (!haveLast_) return false;
  last_.debugger = true;
  relaunch_ = true;
  if (busy()) session_.stop();  // pump() starts the new run once the old one is gone
  return true;
}

void PlayController::pump() {
  if (relaunch_ && !busy()) {
    relaunch_ = false;
    if (thread_.joinable()) thread_.join();
    play(last_);
  }
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
