// F6.05: compile + package + launch for the Build Settings platforms whose flow is not a TargetModule (data: tools/pipeline/data/platforms.json).
#include "rensga/pipeline/platform_build.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "rensga/pipeline/compile_cache.h"
#include "rensga/pipeline/process.h"
#include "rensga/pipeline/process_launcher.h"
#include "rensga/pipeline/toolchain.h"

namespace rensga {
namespace pipeline {
extern const char* const kPlatformsJson;  // generated from tools/pipeline/data/platforms.json
}
}

namespace rensga {
namespace pipeline {

namespace fs = std::filesystem;

namespace {

fs::path repoRoot() {
  if (const char* e = std::getenv("RENSGA_ROOT")) return e;
  return RENSGA_SOURCE_DIR;
}

void note(const EventSink& sink, Stage st, Status s, const std::string& msg) {
  if (!sink) return;
  Event e;
  e.stage = st;
  e.status = s;
  e.message = msg;
  sink(e);
}

int buildJobs() {
  const char* j = std::getenv("RENSGA_JOBS");
  const int n = j ? std::atoi(j) : 0;
  return n > 0 ? n : 2;
}

std::vector<std::string> streamedWavs(const fs::path& pack) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(pack.parent_path(), ec))
    if (e.is_regular_file() && e.path().extension() == ".wav") out.push_back(e.path().string());
  std::sort(out.begin(), out.end());
  return out;
}

bool run(const ProcessSpec& ps, Stage st, const EventSink& sink, const std::atomic<bool>* cancel, const std::string& what) {
  const int rc = runProcess(ps, [&](const std::string& l) { note(sink, st, Status::Progress, l); }, cancel);
  if (rc != 0) note(sink, st, Status::Failed, "command failed (exit " + std::to_string(rc) + "): " + what);
  return rc == 0;
}

// The toolchain of a platform: a native module, Docker, or (linux) the host compiler. `tr` is unresolved for linux.
struct Tool {
  std::unique_ptr<ToolchainRunner> tr;
  bool native = false;
  ProcessSpec spec(std::vector<std::string> cmd, const fs::path& cwd, std::vector<std::pair<std::string, std::string>> env = {}) const {
    if (tr) return tr->spec(std::move(cmd), cwd, std::move(env));
    ProcessSpec ps;
    ps.argv = std::move(cmd);
    ps.cwd = cwd;
    ps.env = std::move(env);
    return ps;
  }
};

bool resolveTool(const PlatformInfo& pi, Stage st, const EventSink& sink, Tool& t) {
  if (pi.moduleId.empty()) return true;
  t.tr.reset(new ToolchainRunner(pi.moduleId, pi.dockerWrapper.empty() ? fs::path() : repoRoot() / "tools" / "docker" / (pi.dockerWrapper + ".sh")));
  std::string err;
  if (!t.tr->resolve(err)) {
    note(sink, st, Status::Failed, err);
    return false;
  }
  t.native = t.tr->backend() == ToolchainBackend::Native;
  note(sink, st, Status::Progress, std::string("toolchain: ") + toolchainBackendName(t.tr->backend()) + " (" + pi.moduleId + ")");
  return true;
}

bool prescan(const PlatformContext& c, const fs::path& dir, const EventSink& sink) {
  ProcessSpec ps;
  ps.argv = {(repoRoot() / "tools" / "reflect" / "prescan.sh").string(), dir.string(), c.project.string()};
  ps.cwd = repoRoot();
  return run(ps, Stage::Compile, sink, c.cancel, "reflection codegen");
}

fs::path adbPath() {
  std::error_code ec;
  const fs::path sdk = repoRoot() / "build" / "android-sdk" / "platform-tools" / "adb";
  if (fs::exists(sdk, ec)) return sdk;
  if (const char* h = std::getenv("ANDROID_HOME")) {
    const fs::path p = fs::path(h) / "platform-tools" / "adb";
    if (fs::exists(p, ec)) return p;
  }
  return "adb";
}

}  // namespace

const std::vector<PlatformInfo>& platformInfos() {
  static const std::vector<PlatformInfo> v = [] {
    std::vector<PlatformInfo> out;
    const nlohmann::json j = nlohmann::json::parse(kPlatformsJson, nullptr, false);
    if (!j.is_object() || !j.contains("platforms")) return out;
    for (const nlohmann::json& e : j["platforms"]) {
      PlatformInfo p;
      p.id = e.value("id", std::string());
      p.name = e.value("name", p.id);
      p.console = e.value("console", false);
      p.flow = e.value("flow", std::string("cmake"));
      p.moduleId = e.value("module", std::string());
      p.dockerWrapper = e.value("docker", std::string());
      p.output = e.value("output", std::string());
      p.devStream = e.value("devStream", std::string("none"));
      p.preset = e.value("preset", std::string());
      p.target = e.value("target", std::string());
      p.artifact = e.value("artifact", std::string());
      for (const char* key : {"cmakeDefaults", "cmakeArgs"})
        if (e.contains(key))
          for (const nlohmann::json& d : e[key]) (std::string(key) == "cmakeDefaults" ? p.cmakeDefaults : p.cmakeArgs).push_back(d.get<std::string>());
      if (e.contains("copyRuntime")) {
        p.copyRuntime.native = e["copyRuntime"].value("native", std::string());
        p.copyRuntime.docker = e["copyRuntime"].value("docker", std::string());
      }
      if (e.contains("package")) {
        const nlohmann::json& k = e["package"];
        p.package.kind = k.value("kind", std::string());
        p.package.script = k.value("script", std::string());
        p.package.file = k.value("file", std::string());
        p.package.exeSuffix = k.value("exeSuffix", std::string());
        p.package.dll = k.value("dll", std::string());
        p.package.suffix = k.value("suffix", std::string());
        p.package.note = k.value("note", std::string());
      }
      if (e.contains("run")) {
        const nlohmann::json& k = e["run"];
        p.run.kind = k.value("kind", std::string());
        p.run.script = k.value("script", std::string());
        p.run.name = k.value("name", std::string());
        p.run.needs = k.value("needs", std::string());
        p.run.packageId = k.value("package", std::string());
        p.run.log = k.value("log", std::string());
        p.run.frames = k.value("frames", 120);
        p.run.timeoutSeconds = k.value("timeoutSeconds", 120);
      }
      if (!p.id.empty()) out.push_back(std::move(p));
    }
    return out;
  }();
  return v;
}

const PlatformInfo* findPlatform(const std::string& id) {
  for (const PlatformInfo& p : platformInfos())
    if (p.id == id) return &p;
  return nullptr;
}

std::vector<std::string> platformsForProject(const fs::path& project) {
  std::string profile;
  try {
    std::ifstream f(project / "ProjectSettings" / "ConsoleTarget.json");
    if (f) profile = nlohmann::json::parse(f).value("profile", std::string());
  } catch (...) {
  }
  // One project = one profile (design doc): its own console first, then the PC / phone players every project also builds for.
  std::vector<std::string> out;
  const PlatformInfo* own = findPlatform(profile);
  if (own && own->console) out.push_back(own->id);
  for (const PlatformInfo& p : platformInfos())
    if (!p.console) out.push_back(p.id);
  return out;
}

std::string cookTargetFor(const std::string& platform) { return platform; }

std::string projectBuildName(const fs::path& project) {
  std::string name;
  try {
    std::ifstream f(project / "ProjectSettings" / "Project.json");
    if (f) name = nlohmann::json::parse(f).value("name", std::string());
  } catch (...) {
  }
  if (name.empty()) name = fs::absolute(project).filename().string();
  std::string out;
  for (char ch : name) out.push_back((isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') ? ch : '_');
  return out.empty() ? "game" : out;
}

fs::path platformOutputDir(const fs::path& project, const std::string& platform, const fs::path& outputRoot) {
  fs::path root = outputRoot.empty() ? project / "Builds" : (outputRoot.is_absolute() ? outputRoot : project / outputRoot);
  return root / platform;
}

bool platformCompile(const PlatformContext& c, const EventSink& sink, fs::path& exe) {
  const PlatformInfo* pi = findPlatform(c.platform);
  if (!pi || pi->moduleFlow()) {
    note(sink, Stage::Compile, Status::Failed, "unknown platform '" + c.platform + "'");
    return false;
  }
  Tool tool;
  if (!resolveTool(*pi, Stage::Compile, sink, tool)) return false;
  std::error_code ec;
  fs::create_directories(c.buildDir, ec);
  {  // refuse a pack the target's renderer cannot draw (texture and mesh formats of another profile) before the slow compile
    ProcessSpec chk;
    chk.argv = {"python3", "-I", (repoRoot() / "tools" / "ci" / "pack_check.py").string(), c.cookedPack.string()};
    chk.cwd = repoRoot();
    std::string out;
    const int rc = runProcess(chk, [&](const std::string& l) { out += l + "\n"; });
    if (rc != 0 && rc != -1) {
      note(sink, Stage::Compile, Status::Failed, "the cooked pack cannot be drawn by the " + c.platform + " renderer: " + out);
      return false;
    }
  }
  const fs::path cmakeDir = c.buildDir / (tool.native ? "cmake-native" : "cmake");
  const std::string devWord = c.development ? "dev" : "release";

  if (pi->flow == "gradle") {
    // The pack and the music go in as APK assets, Gradle builds the debug APK (tools/ci/android.sh does the same).
    const fs::path ab = repoRoot() / "tools" / "android" / "build";
    fs::remove_all(ab / "assets", ec);
    fs::create_directories(ab / "assets", ec);
    fs::copy_file(c.cookedPack, ab / "assets" / "game.rpak", fs::copy_options::overwrite_existing, ec);
    if (ec) { note(sink, Stage::Compile, Status::Failed, "cannot copy the cooked pack " + c.cookedPack.string()); return false; }
    for (const std::string& w : streamedWavs(c.cookedPack)) fs::copy_file(w, ab / "assets" / fs::path(w).filename(), fs::copy_options::overwrite_existing, ec);
    if (!prescan(c, ab / "reflect", sink)) return false;
    fs::create_directories(repoRoot() / "build" / "gradle-cache", ec);
    note(sink, Stage::Compile, Status::Progress, "building the debug APK (Gradle)");
    ProcessSpec ps = tool.spec({(repoRoot() / "tools" / "android" / "build.sh").string()}, repoRoot(),
                               {{"RENSGA_PROJECT_DIR", c.project.string()}, {"GRADLE_USER_HOME", (repoRoot() / "build" / "gradle-cache").string()}});
    if (!run(ps, Stage::Compile, sink, c.cancel, "android build.sh")) return false;
    exe = repoRoot() / pi->artifact;
    if (!fs::exists(exe, ec)) { note(sink, Stage::Compile, Status::Failed, "missing build output " + exe.string()); return false; }
    return true;
  }

  const fs::path built = cmakeDir / pi->artifact;
  const bool devLog = c.development && pi->devStream == "cmake-log";

  // Skip the whole compile when nothing it depends on changed (same idea as the console module's compile stamp).
  InputHasher in;
  in.addString(devWord);
  in.addString(toolchainBackendName(tool.tr ? tool.tr->backend() : ToolchainBackend::None));
  in.addFileContent(c.project / "ProjectSettings" / "ConsoleTarget.json", "ConsoleTarget.json");
  const std::vector<std::string> scripts = in.addTreeContent(c.project / "Assets", {".cpp", ".h", ".hpp", ".cc"});
  for (const char* dir : {"engine", "cmake", "tools/reflect"}) in.addTreeStat(repoRoot() / dir, {".cpp", ".c", ".h", ".hpp", ".txt", ".cmake", ".py", ".in", ".sh"});
  in.addFileContent(repoRoot() / "CMakeLists.txt", "CMakeLists.txt");
  in.addFileContent(repoRoot() / "CMakePresets.json", "CMakePresets.json");
  const std::string fingerprint = in.hex();
  const fs::path stamp = c.buildDir / (cmakeDir.filename().string() + ".compile-stamp");
  if (fs::exists(built, ec) && stampMatches(stamp, fingerprint)) {
    note(sink, Stage::Compile, Status::Progress, "compile skipped: no script, engine or build option changed since the last build");
    exe = built;
    return true;
  }

  note(sink, Stage::Compile, Status::Progress, "reflection codegen");
  if (!prescan(c, cmakeDir / "reflect", sink)) return false;
  std::vector<std::string> cfg = {"cmake", "--preset", pi->preset, "-B", cmakeDir.string(), "-DRENSGA_REFLECT_DIR=" + (cmakeDir / "reflect").string(),
                                  "-DRENSGA_PROJECT_DIR=" + c.project.string()};
  if (tool.native) cfg.insert(cfg.begin() + 3, {"-G", "Ninja"});
  for (const std::string& d : pi->cmakeDefaults) cfg.push_back(d);  // reset the test-only cache entries every run
  for (const std::string& d : pi->cmakeArgs) cfg.push_back(d);
  cfg.push_back(std::string("-DRENSGA_PROF_STREAM_LOG=") + (devLog ? "ON" : "OFF"));
  InputHasher ch;
  for (const std::string& a : cfg) ch.addString(a);
  for (const std::string& n : scripts) ch.addString(n);
  const fs::path cfgStamp = c.buildDir / (cmakeDir.filename().string() + ".configure-stamp");
  const bool tree = fs::exists(cmakeDir / "build.ninja", ec) || fs::exists(cmakeDir / "Makefile", ec);
  if (tree && stampMatches(cfgStamp, ch.hex())) {
    note(sink, Stage::Compile, Status::Progress, "configure skipped (" + cmakeDir.string() + " is up to date)");
  } else {
    note(sink, Stage::Compile, Status::Progress, "configuring " + pi->preset + " preset (" + cmakeDir.string() + ")");
    if (!run(tool.spec(cfg, repoRoot(), {{"RENSGA_PROJECT_DIR", c.project.string()}}), Stage::Compile, sink, c.cancel, "cmake configure")) return false;
    writeStamp(cfgStamp, ch.hex());
  }
  note(sink, Stage::Compile, Status::Progress, "building " + pi->target);
  if (!run(tool.spec({"cmake", "--build", cmakeDir.string(), "--target", pi->target, "-j" + std::to_string(buildJobs())}, repoRoot(),
                     {{"RENSGA_PROJECT_DIR", c.project.string()}}),
           Stage::Compile, sink, c.cancel, "cmake --build"))
    return false;
  const std::string rt = tool.native ? pi->copyRuntime.native : pi->copyRuntime.docker;
  if (!rt.empty()) {  // runtime libraries next to the exe (the module's environment names where they are)
    if (!run(tool.spec({"sh", "-c", "cp " + rt + " '" + built.parent_path().string() + "/'"}, repoRoot()), Stage::Compile, sink, c.cancel, "copy runtime libraries"))
      return false;
  }
  if (!fs::exists(built, ec)) { note(sink, Stage::Compile, Status::Failed, "missing build output " + built.string()); return false; }
  writeStamp(stamp, fingerprint);
  exe = built;
  return true;
}

bool platformPackage(const PlatformContext& c, const fs::path& exe, const EventSink& sink, fs::path& primary) {
  const PlatformInfo* pi = findPlatform(c.platform);
  if (!pi) return false;
  std::error_code ec;
  fs::remove_all(c.outDir, ec);
  fs::create_directories(c.outDir, ec);
  if (ec) { note(sink, Stage::Package, Status::Failed, "cannot create " + c.outDir.string()); return false; }
  auto copy = [&](const fs::path& from, const fs::path& to) {
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) note(sink, Stage::Package, Status::Failed, "cannot copy " + from.string() + " to " + to.string() + ": " + ec.message());
    return !ec;
  };
  const PlatformInfo::Package& pk = pi->package;
  if (pk.kind == "file") {
    primary = c.outDir / (c.name + pk.suffix);
    if (!copy(exe, primary)) return false;
    note(sink, Stage::Package, Status::Progress, "package: " + primary.string() + (pk.note.empty() ? "" : " (" + pk.note + ")"));
    return true;
  }
  if (pk.kind == "script") {
    primary = c.outDir / pk.file;
    ProcessSpec ps;
    ps.argv = {(repoRoot() / pk.script).string(), exe.string(), primary.string(), c.cookedPack.string()};
    for (const std::string& w : streamedWavs(c.cookedPack)) ps.argv.push_back(w);
    ps.cwd = repoRoot();
    if (!run(ps, Stage::Package, sink, c.cancel, pk.script)) return false;
    note(sink, Stage::Package, Status::Progress, "package: " + primary.string() + (pk.note.empty() ? "" : " (" + pk.note + ")"));
    return true;
  }
  // folder: the exe + game.rpak + streamed music (+ the runtime DLL), a folder that starts the game.
  const std::string exeName = c.name + pk.exeSuffix;
  if (!copy(exe, c.outDir / exeName) || !copy(c.cookedPack, c.outDir / "game.rpak")) return false;
  fs::permissions(c.outDir / exeName, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
  if (!pk.dll.empty() && fs::exists(exe.parent_path() / pk.dll, ec) && !copy(exe.parent_path() / pk.dll, c.outDir / pk.dll)) return false;
  for (const std::string& w : streamedWavs(c.cookedPack))
    if (!copy(w, c.outDir / fs::path(w).filename())) return false;
  std::ofstream(c.outDir / "THIRD-PARTY.txt") << "SDL2 (zlib license, https://libsdl.org) is used by this player.\nBuilt with Rensga Engine (MIT).\n";
  primary = c.outDir;
  note(sink, Stage::Package, Status::Progress, "package: " + c.outDir.string() + "/" + exeName);
  return true;
}

LaunchKind platformLaunch(const PlatformContext& c, const fs::path& primary, const LaunchSpec& spec, const EventSink& sink,
                          std::unique_ptr<EmulatorLauncher>& launcher, std::string& reason) {
  const PlatformInfo* pi = findPlatform(c.platform);
  if (!pi) { reason = "unknown platform"; return LaunchKind::Failed; }
  const PlatformInfo::Run& rn = pi->run;
  ProcessLauncher::Config cfg;
  const int frames = spec.quitAfterSeconds > 0 ? spec.quitAfterSeconds * 60 : 0;
  cfg.cwd = c.outDir;
  if (rn.kind == "adb") {
    const fs::path adb = adbPath();
    std::string devices;
    ProcessSpec q;
    q.argv = {adb.string(), "devices"};
    runProcess(q, [&](const std::string& l) { devices += l + "\n"; });
    bool have = false;
    size_t pos = 0;
    while ((pos = devices.find('\n', pos)) != std::string::npos) {
      ++pos;
      const size_t e = devices.find('\n', pos);
      const std::string line = devices.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
      if (line.find("\tdevice") != std::string::npos) have = true;
    }
    if (!have) { reason = "no Android device attached (adb devices is empty): built, not run"; return LaunchKind::NotRun; }
    ProcessSpec inst;
    inst.argv = {adb.string(), "install", "-r", primary.string()};
    if (runProcess(inst, [&](const std::string& l) { note(sink, Stage::Launch, Status::Progress, l); }) != 0) {
      reason = "adb install failed";
      return LaunchKind::Failed;
    }
    cfg.argv = {adb.string(), "shell", "monkey", "-p", rn.packageId, "-c", "android.intent.category.LAUNCHER", "1"};
    cfg.name = "device (adb)";
    cfg.heartbeat = false;
  } else if (rn.kind == "script") {
    cfg.argv = {(repoRoot() / rn.script).string(), primary.string(), "--frames", std::to_string(frames > 0 ? frames : rn.frames), "--timeout",
                std::to_string(rn.timeoutSeconds), "--log", (c.project / rn.log).string()};
    cfg.cwd = repoRoot();
    cfg.name = rn.name;
    cfg.heartbeat = false;
    std::error_code ec;
    fs::create_directories((c.project / rn.log).parent_path(), ec);
  } else if (rn.kind == "wine") {
    ProcessSpec w;
    w.argv = {"sh", "-c", "command -v wine"};
    std::string wine;
    runProcess(w, [&](const std::string& l) { if (wine.empty()) wine = l; });
    if (wine.empty()) {
      reason = "wine is not installed: built, not run (the folder runs on Windows)";
      return LaunchKind::NotRun;
    }
    cfg.argv = {wine, (primary / (c.name + pi->package.exeSuffix)).string()};
    if (frames) { cfg.argv.push_back("--frames"); cfg.argv.push_back(std::to_string(frames)); }
    cfg.name = "wine";
    cfg.heartbeat = false;  // wine + software GL is slow: no hang verdict
    // Same environment as tools/ci/windows_run.sh: a private 64-bit prefix, no mono / gecko prompts, software GL (softpipe does not allocate).
    cfg.env = {{"WINEDEBUG", "-all"}, {"GALLIUM_DRIVER", "softpipe"}, {"LIBGL_ALWAYS_SOFTWARE", "1"}, {"WINEARCH", "win64"}, {"WINEDLLOVERRIDES", "mscoree,mshtml="}};
    if (!std::getenv("WINEPREFIX")) cfg.env.push_back({"WINEPREFIX", (repoRoot() / "build" / "wine-prefix").string()});
    if (c.development && pi->devStream == "env-log") cfg.env.push_back({"RENSGA_PROF_STREAM", "log"});
  } else {  // process
    cfg.argv = {(primary / (c.name + pi->package.exeSuffix)).string()};
    if (frames) { cfg.argv.push_back("--frames"); cfg.argv.push_back(std::to_string(frames)); }
    cfg.name = "player";
    if (c.development && pi->devStream == "env-tcp") cfg.env.push_back({"RENSGA_PROF_STREAM", "tcp"});
    if (c.development && pi->devStream == "env-log") cfg.env.push_back({"RENSGA_PROF_STREAM", "log"});
  }
  launcher.reset(new ProcessLauncher(cfg));
  if (!launcher->start(spec, sink)) {
    reason = "could not start " + cfg.name;
    launcher.reset();
    return LaunchKind::Failed;
  }
  return LaunchKind::Started;
}

bool collectFolder(const fs::path& from, const fs::path& to, const EventSink& sink) {
  std::error_code ec;
  fs::remove_all(to, ec);
  fs::create_directories(to, ec);
  for (const char* name : {"EBOOT.PBP", "game.rpak"}) {
    if (!fs::exists(from / name, ec)) continue;
    fs::copy_file(fs::canonical(from / name, ec), to / name, fs::copy_options::overwrite_existing, ec);
    if (ec) { note(sink, Stage::Package, Status::Failed, std::string("cannot copy ") + name + ": " + ec.message()); return false; }
  }
  for (const auto& e : fs::directory_iterator(from, ec))
    if (e.is_regular_file() && e.path().extension() == ".wav") fs::copy_file(e.path(), to / e.path().filename(), fs::copy_options::overwrite_existing, ec);
  if (fs::exists(from / "LICENSES", ec)) fs::copy(from / "LICENSES", to / "LICENSES", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  note(sink, Stage::Package, Status::Progress, "output: " + to.string());
  return true;
}

}  // namespace pipeline
}  // namespace rensga
