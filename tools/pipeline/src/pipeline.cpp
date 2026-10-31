// MVP-34: Validate -> Cook -> Compile -> Package -> Launch. Never names a console: the target module
// comes from TargetRegistry by id (from the request or ProjectSettings/Project.json).
#include "rensga/pipeline/pipeline.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"
#include "rensga/editor/target_module.h"
#include "rensga/pipeline/emulator_launcher.h"

namespace rensga {
namespace pipeline {

namespace fs = std::filesystem;

struct Session::Impl {
  std::atomic<bool> building{false};
  std::atomic<bool> cancel{false};
  std::mutex mu;
  editor::TargetModule* launched = nullptr;
};

const char* stageName(Stage s) {
  switch (s) {
    case Stage::Validate: return "validate";
    case Stage::Cook: return "cook";
    case Stage::Compile: return "compile";
    case Stage::Package: return "package";
    case Stage::Launch: return "launch";
  }
  return "?";
}
const char* statusName(Status s) {
  switch (s) {
    case Status::Started: return "started";
    case Status::Progress: return "progress";
    case Status::Succeeded: return "succeeded";
    case Status::Failed: return "failed";
    case Status::Skipped: return "skipped";
  }
  return "?";
}

Session::Session() : impl_(new Impl) {}
Session::~Session() {
  stop();
  delete impl_;
}

namespace {

void emit(const EventSink& sink, Stage st, Status s, std::string msg, std::string guid = {}) {
  if (!sink) return;
  Event e;
  e.stage = st;
  e.status = s;
  e.message = std::move(msg);
  e.assetGuid = std::move(guid);
  sink(e);
}

using Clock = std::chrono::steady_clock;
long msSince(Clock::time_point t) {
  return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count());
}
// F5.21: 'pipeline: stage <name> <ms>' after every stage; the editor console and the pipeline tests read them.
void stageTime(const EventSink& sink, Stage st, Clock::time_point t0) {
  emit(sink, st, Status::Progress, std::string("pipeline: stage ") + stageName(st) + " " + std::to_string(msSince(t0)) + " ms");
}

bool readTargetFromProject(const fs::path& project, std::string& target, std::string& err) {
  std::ifstream f(project / "ProjectSettings" / "Project.json");
  if (!f) {
    err = "cannot open " + (project / "ProjectSettings" / "Project.json").string();
    return false;
  }
  try {
    nlohmann::json j = nlohmann::json::parse(f);
    target = j.value("target", std::string());
  } catch (const std::exception& e) {
    err = std::string("Project.json: ") + e.what();
    return false;
  }
  if (target.empty()) err = "Project.json has no \"target\" and --target was not given";
  return !target.empty();
}

}  // namespace

bool Session::run(const BuildRequest& request, EventSink sink) {
  bool expected = false;
  if (!impl_->building.compare_exchange_strong(expected, true)) {
    emit(sink, Stage::Validate, Status::Failed, "a pipeline run is already in progress");
    return false;
  }
  struct Reset {
    std::atomic<bool>& b;
    ~Reset() { b = false; }
  } reset{impl_->building};
  stop();  // a previous Launch may still be alive
  impl_->cancel = false;

  const Clock::time_point tStart = Clock::now();
  Clock::time_point tStage = tStart;
  std::error_code ec;
  fs::path project = fs::absolute(request.project, ec);
  std::string target = request.target, err;

  emit(sink, Stage::Validate, Status::Started, "validating " + project.string());
  if (!fs::exists(project / "ProjectSettings" / "Project.json")) {
    emit(sink, Stage::Validate, Status::Failed, "not a Rensga project (missing ProjectSettings/Project.json): " + project.string());
    return false;
  }
  if (target.empty() && !readTargetFromProject(project, target, err)) {
    emit(sink, Stage::Validate, Status::Failed, err);
    return false;
  }
  editor::TargetModule* module = editor::TargetRegistry::find(target);
  if (!module) {
    emit(sink, Stage::Validate, Status::Failed, "unknown target '" + target + "'");
    return false;
  }

  editor::BuildContext ctx;
  ctx.project = project;
  ctx.release = request.release;
  ctx.quitAfterSeconds = request.quitAfterSeconds;
  ctx.cookedPack = project / "Library" / "Cooked" / target / "game.rpak";
  ctx.buildDir = project / "Library" / "Build" / (target + (request.release ? "-release" : "-dev"));
  ctx.generatedDir = project / "Library" / "Generated" / target;
  ctx.cancel = &impl_->cancel;

  if (!module->validate(ctx, sink)) {
    emit(sink, Stage::Validate, Status::Failed, "validation failed");
    return false;
  }
  emit(sink, Stage::Validate, Status::Succeeded, "project is valid for target " + target);
  stageTime(sink, Stage::Validate, tStage);
  tStage = Clock::now();
  if (request.lastStage == Stage::Validate) return true;

  // ---- Cook ----
  emit(sink, Stage::Cook, Status::Started, "cooking for " + target);
  CookOptions opt;
  opt.project = project;
  opt.target = target;
  opt.genHeader = ctx.generatedDir / "console_target.gen.h";
  CookResult cr = cookProject(opt, [&](const CookMessage& m) {
    Status st = m.severity == CookMessage::Error ? Status::Failed : Status::Progress;
    std::string text = m.text;
    if (!m.asset.empty()) text += " [" + m.asset + "]";
    emit(sink, Stage::Cook, st, text, m.assetGuid);
  });
  if (!cr.ok) {
    if (cr.errors.empty()) emit(sink, Stage::Cook, Status::Failed, "cook failed");
    return false;
  }
  if (cr.upToDate) {
    emit(sink, Stage::Cook, Status::Succeeded, "up to date: " + cr.packPath.string());
  } else {
    char buf[256];
    snprintf(buf, sizeof buf, "cooked: pack %llu bytes, %llu triangles, %llu objects",
             (unsigned long long)cr.usage.packBytes,
             (unsigned long long)cr.usage.triangles, (unsigned long long)cr.usage.objects);
    emit(sink, Stage::Cook, Status::Succeeded, buf);
  }
  stageTime(sink, Stage::Cook, tStage);
  tStage = Clock::now();
  if (request.lastStage == Stage::Cook) return true;

  // ---- Compile ----
  emit(sink, Stage::Compile, Status::Started, std::string("building runtime (") + (request.release ? "release" : "dev") + ")");
  std::error_code mk;
  fs::create_directories(ctx.buildDir, mk);
  if (!module->compile(ctx, sink)) {
    emit(sink, Stage::Compile, Status::Failed, "compile failed");
    return false;
  }
  emit(sink, Stage::Compile, Status::Succeeded, "runtime built in " + ctx.buildDir.string());
  stageTime(sink, Stage::Compile, tStage);
  tStage = Clock::now();
  if (request.lastStage == Stage::Compile) return true;

  // ---- Package ----
  if (!request.release) {
    emit(sink, Stage::Package, Status::Skipped, "dev build boots the ELF directly, the game reads Library/Cooked in place through host0:");
  } else {
    emit(sink, Stage::Package, Status::Started, "packaging");
    if (!module->package(ctx, sink)) {
      emit(sink, Stage::Package, Status::Failed, "packaging failed");
      return false;
    }
    emit(sink, Stage::Package, Status::Succeeded, "packaged in " + ctx.buildDir.string());
  }
  if (request.lastStage == Stage::Package || !request.launch) return true;

  // ---- Launch ----
  emit(sink, Stage::Launch, Status::Started, "launching emulator");
  LaunchSpec spec;
  spec.workingDir = ctx.buildDir;
  spec.configDir = project / "Library" / "Emulators";
  spec.logPath = project / "Library" / "Logs" / "last-play.log";
  spec.quitAfterSeconds = request.quitAfterSeconds;
  spec.headless = request.headless;
  spec.debugger = request.debugger;
  spec.embed = request.embed;
  spec.hangTimeoutSeconds = request.hangTimeoutSeconds;
  if (!module->launcher().start(spec, sink)) {
    emit(sink, Stage::Launch, Status::Failed, "could not start the emulator");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->launched = module;
  }
  stageTime(sink, Stage::Launch, tStage);
  emit(sink, Stage::Launch, Status::Progress, "pipeline: ready " + std::to_string(msSince(tStart)) + " ms after Play");
  emit(sink, Stage::Launch, Status::Succeeded, "emulator running");
  return true;
}

void Session::stop() {
  impl_->cancel = true;
  editor::TargetModule* m;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    m = impl_->launched;
  }
  if (m) m->launcher().stop();
}

bool Session::running() const {
  if (impl_->building) return true;
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->launched && impl_->launched->launcher().running();
}

namespace {
editor::TargetModule* launchedOf(std::mutex& mu, editor::TargetModule* const& m) {
  std::lock_guard<std::mutex> lock(mu);
  return m;
}
}  // namespace

bool Session::pause() { auto* m = launchedOf(impl_->mu, impl_->launched); return m && m->launcher().pause(); }
bool Session::resume() { auto* m = launchedOf(impl_->mu, impl_->launched); return m && m->launcher().resume(); }
bool Session::step(int frames) { auto* m = launchedOf(impl_->mu, impl_->launched); return m && m->launcher().step(frames); }
bool Session::command(const std::string& raw) { auto* m = launchedOf(impl_->mu, impl_->launched); return m && m->launcher().sendCommand(raw); }
bool Session::paused() const { auto* m = launchedOf(impl_->mu, impl_->launched); return m && m->launcher().paused(); }
ExitState Session::exitState() const {
  auto* m = launchedOf(impl_->mu, impl_->launched);
  return m ? m->launcher().exitState() : ExitState::Exited;
}
int Session::exitCode() const { auto* m = launchedOf(impl_->mu, impl_->launched); return m ? m->launcher().exitCode() : -1; }
int Session::exitSignal() const { auto* m = launchedOf(impl_->mu, impl_->launched); return m ? m->launcher().exitSignal() : 0; }
std::string Session::exitDetail() const { auto* m = launchedOf(impl_->mu, impl_->launched); return m ? m->launcher().exitDetail() : std::string(); }
bool Session::saveLog(const std::filesystem::path& path) const { auto* m = launchedOf(impl_->mu, impl_->launched); return m && m->launcher().saveLog(path); }

std::vector<std::string> Session::drainGameLog() {
  editor::TargetModule* m;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    m = impl_->launched;
  }
  return m ? m->launcher().drainLog() : std::vector<std::string>{};
}

}  // namespace pipeline
}  // namespace rensga
