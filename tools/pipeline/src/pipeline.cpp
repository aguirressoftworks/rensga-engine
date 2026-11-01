// MVP-34: Validate -> Cook -> Compile -> Package -> Launch. Never names a console: the target module
// comes from TargetRegistry by id (from the request or ProjectSettings/Project.json).
#include "rensga/pipeline/pipeline.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"
#include "rensga/editor/target_module.h"
#include "rensga/pipeline/emulator_launcher.h"
#include "rensga/pipeline/platform_build.h"
#include "rensga/editor/project_settings.h"

namespace rensga {
namespace pipeline {

namespace fs = std::filesystem;

struct Session::Impl {
  std::atomic<bool> building{false};
  std::atomic<bool> cancel{false};
  std::mutex mu;
  editor::TargetModule* launched = nullptr;
  // F6.05: Build Settings platforms that are not TargetModules launch through their own launcher.
  std::unique_ptr<EmulatorLauncher> platformLauncher;
  std::filesystem::path output;
  std::string note;
  bool notRun = false;
  EmulatorLauncher* launcher() {  // caller holds mu
    if (platformLauncher) return platformLauncher.get();
    return launched ? &launched->launcher() : nullptr;
  }
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

bool Session::run(const BuildRequest& requestIn, EventSink sink) {
  BuildRequest request = requestIn;
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
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->platformLauncher.reset();
    impl_->launched = nullptr;
    impl_->output.clear();
    impl_->note.clear();
    impl_->notRun = false;
  }
  impl_->cancel = false;
  const PlatformInfo* platformInfo = request.platform.empty() ? nullptr : findPlatform(request.platform);
  if (!request.platform.empty() && !platformInfo) {
    emit(sink, Stage::Validate, Status::Failed, "unknown platform '" + request.platform + "' (rensga platforms lists them)");
    return false;
  }
  if (platformInfo && !platformInfo->moduleFlow()) return runPlatform(request, sink);
  if (platformInfo) {  // Build Settings on a module-flow platform: the TargetModule does the work; release package unless development
    request.target = platformInfo->id;
    request.release = !request.development;
  }

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
  ctx.development = request.development || !request.release;
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
  bool voiceOver = false;
  if (!request.platform.empty()) {
    editor::ProjectBuildSettings pbs;
    std::string perr;
    if (editor::loadProjectBuild(project, pbs, perr) && pbs.enabledScenes().empty()) {
      emit(sink, Stage::Validate, Status::Failed, "no scene is enabled in Build Settings (Project.json scenes is empty)");
      return false;
    }
    opt.target = target;
  }
  CookResult cr = cookProject(opt, [&](const CookMessage& m) {
    if (!request.platform.empty() && m.severity == CookMessage::Warning && m.text.rfind("audio voices in", 0) == 0) voiceOver = true;
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
  if (voiceOver) {  // Build Settings: a scene that starts more AudioSources than the target has voices blocks the build
    emit(sink, Stage::Validate, Status::Failed, "a scene starts more audio sources than the target has voices (see the warning above); lower the count or raise ConsoleTarget audio.voices");
    return false;
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
  if (!request.platform.empty() && request.release) {  // F6.05: the finished build goes to <outputDir>/psp
    const fs::path out = platformOutputDir(project, request.platform, request.outputDir);
    if (!collectFolder(ctx.buildDir, out, sink)) return false;
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->output = out / platformInfo->output;  // the image file the module packaged
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

// F6.05: Build Settings for the platforms whose flow is not a TargetModule: the same five stages.
bool Session::runPlatform(const BuildRequest& request, const EventSink& sink) {
  const Clock::time_point tStart = Clock::now();
  Clock::time_point tStage = tStart;
  std::error_code ec;
  const fs::path project = fs::absolute(request.project, ec);
  emit(sink, Stage::Validate, Status::Started, "validating " + project.string() + " for " + request.platform);
  if (!fs::exists(project / "ProjectSettings" / "Project.json")) {
    emit(sink, Stage::Validate, Status::Failed, "not a Rensga project (missing ProjectSettings/Project.json): " + project.string());
    return false;
  }
  const PlatformInfo* pi = findPlatform(request.platform);
  if (!pi) {
    emit(sink, Stage::Validate, Status::Failed, "unknown platform '" + request.platform + "'");
    return false;
  }
  const std::vector<std::string> allowed = platformsForProject(project);
  if (std::find(allowed.begin(), allowed.end(), pi->id) == allowed.end()) {
    std::string l;
    for (const std::string& a : allowed) l += (l.empty() ? "" : ", ") + a;
    emit(sink, Stage::Validate, Status::Failed, pi->name + " builds need a project with the " + pi->id + " profile (ConsoleTarget.json); one project has one profile. This project builds for: " + l);
    return false;
  }
  editor::ProjectBuildSettings pbs;
  std::string perr;
  if (!editor::loadProjectBuild(project, pbs, perr)) {
    emit(sink, Stage::Validate, Status::Failed, "Project.json: " + perr);
    return false;
  }
  if (pbs.enabledScenes().empty()) {
    emit(sink, Stage::Validate, Status::Failed, "no scene is enabled in Build Settings (add scenes to the build first)");
    return false;
  }
  emit(sink, Stage::Validate, Status::Succeeded,
       std::to_string(pbs.enabledScenes().size()) + " scene(s) in the build; budgets are checked per scene while cooking for " + pi->id);
  stageTime(sink, Stage::Validate, tStage);
  tStage = Clock::now();
  if (request.lastStage == Stage::Validate) return true;

  // ---- Cook (validates every scene of the build against the target budgets; errors name the scene and the heaviest object) ----
  const std::string cookTarget = cookTargetFor(pi->id);
  emit(sink, Stage::Cook, Status::Started, "cooking for " + cookTarget);
  CookOptions opt;
  opt.project = project;
  opt.target = cookTarget;
  bool voiceOver = false;
  CookResult cr = cookProject(opt, [&](const CookMessage& m) {
    if (m.severity == CookMessage::Warning && m.text.rfind("audio voices in", 0) == 0) voiceOver = true;
    std::string text = m.text;
    if (!m.asset.empty()) text += " [" + m.asset + "]";
    emit(sink, Stage::Cook, m.severity == CookMessage::Error ? Status::Failed : Status::Progress, text, m.assetGuid);
  });
  if (!cr.ok) {
    if (cr.errors.empty()) emit(sink, Stage::Cook, Status::Failed, "cook failed");
    return false;
  }
  if (voiceOver) {
    emit(sink, Stage::Validate, Status::Failed, "a scene starts more audio sources than the target has voices (see the warning above); lower the count or raise ConsoleTarget audio.voices");
    return false;
  }
  emit(sink, Stage::Cook, Status::Succeeded, (cr.upToDate ? "up to date: " : "cooked: ") + cr.packPath.string());
  stageTime(sink, Stage::Cook, tStage);
  tStage = Clock::now();
  if (request.lastStage == Stage::Cook) return true;

  PlatformContext pc;
  pc.project = project;
  pc.platform = pi->id;
  pc.development = request.development;
  pc.cookedPack = cr.packPath;
  pc.buildDir = project / "Library" / "Build" / (pi->id + (request.development ? "-dev" : "-release"));
  pc.outDir = platformOutputDir(project, pi->id, request.outputDir);
  pc.name = projectBuildName(project);
  pc.cancel = &impl_->cancel;

  // ---- Compile ----
  emit(sink, Stage::Compile, Status::Started, std::string("building the ") + pi->id + " player (" + (request.development ? "development" : "release") + ")");
  fs::path exe;
  if (!platformCompile(pc, sink, exe)) {
    emit(sink, Stage::Compile, Status::Failed, "compile failed");
    return false;
  }
  emit(sink, Stage::Compile, Status::Succeeded, "built " + exe.string());
  stageTime(sink, Stage::Compile, tStage);
  tStage = Clock::now();
  if (request.lastStage == Stage::Compile) return true;

  // ---- Package ----
  emit(sink, Stage::Package, Status::Started, "packaging into " + pc.outDir.string());
  fs::path primary;
  if (!platformPackage(pc, exe, sink, primary)) {
    emit(sink, Stage::Package, Status::Failed, "packaging failed");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->output = primary;
  }
  emit(sink, Stage::Package, Status::Succeeded, "output: " + primary.string());
  stageTime(sink, Stage::Package, tStage);
  tStage = Clock::now();
  if (request.lastStage == Stage::Package || !request.launch) return true;

  // ---- Launch ----
  emit(sink, Stage::Launch, Status::Started, "launching " + pi->name);
  LaunchSpec spec;
  spec.workingDir = pc.outDir;
  spec.quitAfterSeconds = request.quitAfterSeconds;
  spec.headless = request.headless;
  spec.hangTimeoutSeconds = request.hangTimeoutSeconds;
  std::unique_ptr<EmulatorLauncher> launcher;
  std::string reason;
  const LaunchKind lk = platformLaunch(pc, primary, spec, sink, launcher, reason);
  if (lk == LaunchKind::Failed) {
    emit(sink, Stage::Launch, Status::Failed, reason);
    return false;
  }
  if (lk == LaunchKind::NotRun) {
    {
      std::lock_guard<std::mutex> lock(impl_->mu);
      impl_->notRun = true;
      impl_->note = reason;
    }
    emit(sink, Stage::Launch, Status::Skipped, reason);
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->platformLauncher = std::move(launcher);
  }
  stageTime(sink, Stage::Launch, tStage);
  emit(sink, Stage::Launch, Status::Progress, "pipeline: ready " + std::to_string(msSince(tStart)) + " ms after Play");
  emit(sink, Stage::Launch, Status::Succeeded, pi->name + " running");
  return true;
}

void Session::stop() {
  impl_->cancel = true;
  EmulatorLauncher* l;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    l = impl_->launcher();
  }
  if (l) l->stop();
}

bool Session::running() const {
  if (impl_->building) return true;
  EmulatorLauncher* l;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    l = impl_->launcher();
  }
  return l && l->running();
}

#define RENSGA_LAUNCHER(var)                          \
  EmulatorLauncher* var;                              \
  {                                                   \
    std::lock_guard<std::mutex> lock(impl_->mu);      \
    var = impl_->launcher();                          \
  }

bool Session::pause() { RENSGA_LAUNCHER(l); return l && l->pause(); }
bool Session::resume() { RENSGA_LAUNCHER(l); return l && l->resume(); }
bool Session::step(int frames) { RENSGA_LAUNCHER(l); return l && l->step(frames); }
bool Session::command(const std::string& raw) { RENSGA_LAUNCHER(l); return l && l->sendCommand(raw); }
bool Session::paused() const { RENSGA_LAUNCHER(l); return l && l->paused(); }
ExitState Session::exitState() const { RENSGA_LAUNCHER(l); return l ? l->exitState() : ExitState::Exited; }
int Session::exitCode() const { RENSGA_LAUNCHER(l); return l ? l->exitCode() : -1; }
int Session::exitSignal() const { RENSGA_LAUNCHER(l); return l ? l->exitSignal() : 0; }
std::string Session::exitDetail() const { RENSGA_LAUNCHER(l); return l ? l->exitDetail() : std::string(); }
bool Session::saveLog(const std::filesystem::path& path) const { RENSGA_LAUNCHER(l); return l && l->saveLog(path); }
std::vector<std::string> Session::drainGameLog() { RENSGA_LAUNCHER(l); return l ? l->drainLog() : std::vector<std::string>{}; }
std::filesystem::path Session::lastOutput() const { std::lock_guard<std::mutex> lock(impl_->mu); return impl_->output; }
bool Session::builtNotRun() const { std::lock_guard<std::mutex> lock(impl_->mu); return impl_->notRun; }
std::string Session::lastNote() const { std::lock_guard<std::mutex> lock(impl_->mu); return impl_->note; }

}  // namespace pipeline
}  // namespace rensga
