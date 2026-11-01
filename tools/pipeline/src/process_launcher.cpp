#include "rensga/pipeline/process_launcher.h"

#include <signal.h>

#include <fstream>

namespace rensga {
namespace pipeline {

using Clock = std::chrono::steady_clock;

bool ProcessLauncher::start(const LaunchSpec& spec, EventSink sink) {
  stop();
  {
    std::lock_guard<std::mutex> g(mu_);
    pending_.clear();
    all_.clear();
  }
  stopAsked_ = paused_ = false;
  {
    std::lock_guard<std::mutex> once(settleMu_);
    settled_ = false;
  }
  sawGame_ = false;
  code_ = -1;
  sig_ = 0;
  hangSec_ = spec.hangTimeoutSeconds;
  bootSec_ = spec.bootTimeoutSeconds;
  ProcessSpec ps;
  ps.argv = cfg_.argv;
  for (const std::string& a : spec.args) ps.argv.push_back(a);
  ps.cwd = cfg_.cwd.empty() ? spec.workingDir : cfg_.cwd;
  ps.env = cfg_.env;
  for (auto& kv : spec.env) ps.env.push_back(kv);
  t0_ = lastLine_ = Clock::now();
  started_ = proc_.start(ps, [this](const std::string& l) {
    std::lock_guard<std::mutex> g(mu_);
    pending_.push_back(l);
    all_.push_back(l);
    lastLine_ = Clock::now();
    if (l.find("rensga:") != std::string::npos) sawGame_ = true;
  });
  if (!started_ && sink) {
    Event e;
    e.stage = Stage::Launch;
    e.status = Status::Failed;
    e.message = "could not start " + (cfg_.name.empty() ? cfg_.argv.empty() ? std::string("the game") : cfg_.argv[0] : cfg_.name);
    sink(e);
  }
  return started_;
}

void ProcessLauncher::stop() {
  if (!started_) return;
  stopAsked_ = true;
  if (paused_) proc_.signalGroup(SIGCONT);
  proc_.terminate(1500);
  settle();
}

void ProcessLauncher::settle() const {
  std::lock_guard<std::mutex> once(settleMu_);  // the first caller reaps and reads the status, the others wait for it
  if (settled_) return;
  code_ = proc_.wait();
  sig_ = proc_.exitSignal();
  if (code_ == -1) {
    std::lock_guard<std::mutex> g(mu_);
    pending_.push_back("[launcher] could not read the exit status (waitpid errno " + std::to_string(proc_.waitErrno()) + ")");
  }
  settled_ = true;
}

bool ProcessLauncher::running() const {
  if (!started_) return false;
  const bool r = proc_.running();
  if (!r) settle();
  return r;
}

std::vector<std::string> ProcessLauncher::drainLog() {
  std::vector<std::string> out;
  std::lock_guard<std::mutex> g(mu_);
  while (!pending_.empty()) {
    out.push_back(std::move(pending_.front()));
    pending_.pop_front();
  }
  return out;
}

bool ProcessLauncher::pause() {
  if (!running() || paused_) return false;
  if (!proc_.signalGroup(SIGSTOP)) return false;
  paused_ = true;
  return true;
}
bool ProcessLauncher::resume() {
  if (!paused_) return false;
  proc_.signalGroup(SIGCONT);
  paused_ = false;
  lastLine_ = Clock::now();
  return true;
}

ExitState ProcessLauncher::exitState() const {
  if (!started_) return ExitState::Exited;
  if (running()) {
    if (paused_) return ExitState::Running;
    const double since = std::chrono::duration<double>(Clock::now() - lastLine_).count();
    const double alive = std::chrono::duration<double>(Clock::now() - t0_).count();
    if (cfg_.heartbeat) {
      if (sawGame_ && hangSec_ > 0 && since > hangSec_) return ExitState::Hung;
      if (!sawGame_ && bootSec_ > 0 && alive > bootSec_) return ExitState::Hung;
    }
    return ExitState::Running;
  }
  if (stopAsked_) return ExitState::Exited;
  return (sig_ != 0 || code_ != 0) ? ExitState::Crashed : ExitState::Exited;
}

std::string ProcessLauncher::exitDetail() const {
  const ExitState st = exitState();
  const std::string who = cfg_.name.empty() ? "game" : cfg_.name;
  switch (st) {
    case ExitState::Running: return who + " running";
    case ExitState::Hung: return who + " hung: no heartbeat for " + std::to_string(hangSec_) + " s";
    case ExitState::Crashed:
      return sig_ ? who + " crashed: killed by signal " + std::to_string(sig_) : who + " failed: exit code " + std::to_string(code_);
    case ExitState::Exited: return who + (stopAsked_ ? " stopped" : " exited normally");
  }
  return {};
}

bool ProcessLauncher::saveLog(const std::filesystem::path& path) const {
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream f(path);
  if (!f) return false;
  std::lock_guard<std::mutex> g(mu_);
  for (const std::string& l : all_) f << l << "\n";
  return true;
}

}  // namespace pipeline
}  // namespace rensga
