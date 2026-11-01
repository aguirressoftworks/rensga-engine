// PSP target module: cooker plugin lookup, compile with the PSP Build Support module (native; Docker as contributor fallback), EBOOT packaging, PPSSPP launcher.
#include <cstring>
#include <fstream>
#include <tuple>
#include <sstream>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"
#include "rensga/editor/target_module.h"
#include "rensga/pipeline/compile_cache.h"
#include "rensga/pipeline/process.h"
#include "rensga/pipeline/toolchain.h"
#include "ppsspp_launcher.h"
#include "psp_package.h"
#include "psp_vram.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using pipeline::Event;
using pipeline::ProcessSpec;
using pipeline::Stage;
using pipeline::Status;

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

constexpr const char* kModuleId = "psp-build";

// PSP-1000 limits (F4.02). RAM is the same data the cooker's budget check uses (profileRamLimitBytes: 32 MiB, PSP-1000);
// the GU framebuffer is 16-bit 5650 (psp_vram.cpp, engine/backends/render/gu).
constexpr uint32_t kPspFramebufferBpp = 16;
uint64_t pspRamLimitBytes(const ConsoleTargetData& t) {
  if (t.ramLimitBytes) return t.ramLimitBytes;
  const uint64_t p = profileRamLimitBytes("psp");
  return p ? p : 32ull * 1024 * 1024;
}

// "framebuffer".bpp as written in ConsoleTarget.json; a missing field means the PSP default (16).
uint32_t declaredFramebufferBpp(const fs::path& file) {
  try {
    std::ifstream f(file);
    nlohmann::json j = nlohmann::json::parse(f);
    if (j.contains("framebuffer") && j["framebuffer"].is_object() && j["framebuffer"].contains("bpp"))
      return j["framebuffer"]["bpp"].get<uint32_t>();
  } catch (...) {
  }
  return kPspFramebufferBpp;
}

pipeline::ToolchainRunner makeRunner() {
  return pipeline::ToolchainRunner(kModuleId, repoRoot() / "tools" / "docker" / "psp.sh");
}

// Picks native module or Docker (RENSGA_TOOLCHAIN=auto|native|docker); fails with how to install the module.
bool resolveToolchain(pipeline::ToolchainRunner& tr, Stage stage, const EventSink& sink) {
  std::string err;
  if (!tr.resolve(err)) {
    note(sink, stage, Status::Failed, err);
    return false;
  }
  note(sink, stage, Status::Progress, std::string("toolchain: ") + pipeline::toolchainBackendName(tr.backend()) + " (" + kModuleId + ")");
  return true;
}

std::string exitAfterFrames() {
  const char* e = std::getenv("RENSGA_PSP_EXIT_AFTER_FRAMES");
  int n = e ? std::atoi(e) : 0;
  return std::to_string(n > 0 ? n : 0);
}

std::string extraCmakeArgs() {
  const char* e = std::getenv("RENSGA_PSP_CMAKE_ARGS");
  return e ? e : "";
}

int buildJobs() {
  const char* j = std::getenv("RENSGA_JOBS");
  int n = j ? std::atoi(j) : 0;
  return n > 0 ? n : 4;
}

// Runs a build tool through the resolved toolchain, streaming output as Progress events.
bool toolRun(pipeline::ToolchainRunner& tr, const BuildContext& ctx, Stage stage, const EventSink& sink, std::vector<std::string> cmd) {
  const std::string what = cmd.empty() ? "" : cmd[0] + (cmd.size() > 1 ? " " + cmd[1] : "");
  ProcessSpec ps = tr.spec(std::move(cmd), repoRoot(), {{"RENSGA_PROJECT_DIR", ctx.project.string()}});
  int rc = pipeline::runProcess(ps, [&](const std::string& l) { note(sink, stage, Status::Progress, l); }, ctx.cancel);
  if (rc != 0) note(sink, stage, Status::Failed, "command failed (exit " + std::to_string(rc) + "): " + what);
  return rc == 0;
}

// Reflection codegen runs on the host (the pspdev image has no Python); the Docker configure reads its output.
bool prescanReflection(const BuildContext& ctx, const EventSink& sink, const fs::path& reflectDir) {
  ProcessSpec ps;
  ps.argv = {(repoRoot() / "tools" / "reflect" / "prescan.sh").string(), reflectDir.string(), ctx.project.string()};
  ps.cwd = repoRoot();
  int rc = pipeline::runProcess(
      ps, [&](const std::string& l) { note(sink, Stage::Compile, Status::Progress, l); }, ctx.cancel);
  if (rc != 0) note(sink, Stage::Compile, Status::Failed, "reflection codegen failed (exit " + std::to_string(rc) + ")");
  return rc == 0;
}

// A release build dir (Library/Build/psp-release) holds the ELF too, but Build and Run must boot what a player gets: EBOOT.PBP.
class ReleaseAwareLauncher : public PpssppLauncher {
 public:
  bool start(const pipeline::LaunchSpec& spec, pipeline::EventSink sink) override {
    pipeline::LaunchSpec s = spec;
    std::error_code ec;
    const fs::path eboot = spec.workingDir / "EBOOT.PBP";
    if (s.executable.empty() && spec.workingDir.filename().string().size() > 8 && spec.workingDir.filename().string().rfind("-release") != std::string::npos && fs::exists(eboot, ec))
      s.executable = eboot;
    return PpssppLauncher::start(s, std::move(sink));
  }
};

// Width and height from a PNG's IHDR; false when the file is not a PNG.
bool pngSize(const fs::path& file, uint32_t& w, uint32_t& h) {
  std::ifstream f(file, std::ios::binary);
  unsigned char b[24];
  if (!f.read(reinterpret_cast<char*>(b), sizeof b) || std::memcmp(b, "\x89PNG\r\n\x1a\n", 8) != 0) return false;
  w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
  h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
  return true;
}

class PspTargetModule : public TargetModule {
 public:
  std::string_view id() const override { return "psp"; }
  std::string_view displayName() const override { return "PSP"; }

  ICookerFormatPlugin& cookerPlugin() override {
    ICookerFormatPlugin* p = CookerPluginRegistry::instance().find("psp");
    static NoPlugin none;
    return p ? *p : none;
  }

  // F3.02 / F4.04: the VRAM layout lives in psp_vram.cpp (it must match engine/backends/render/gu).
  VramLayout vramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items) const override {
    return pspVramLayout(ct, items);
  }

  bool validate(const BuildContext& ctx, EventSink sink) override {
    try {
      ConsoleTargetData t = loadConsoleTarget(ctx.project / "ProjectSettings" / "ConsoleTarget.json");
      if (t.profile != "psp") {
        note(sink, Stage::Validate, Status::Failed, "ConsoleTarget.json profile is '" + t.profile + "', expected 'psp'");
        return false;
      }
      if (t.scalar != "float") {
        note(sink, Stage::Validate, Status::Failed, "PSP profile requires scalar \"float\" in ConsoleTarget.json");
        return false;
      }
      if (t.ramBytes > pspRamLimitBytes(t)) {
        note(sink, Stage::Validate, Status::Failed,
             "ConsoleTarget.json budgets.ramBytes " + std::to_string(t.ramBytes) + " is over the PSP-1000 limit of " +
                 std::to_string(pspRamLimitBytes(t)) + " bytes (32 MiB)");
        return false;
      }
      const uint32_t bpp = declaredFramebufferBpp(ctx.project / "ProjectSettings" / "ConsoleTarget.json");
      if (bpp != kPspFramebufferBpp) {
        note(sink, Stage::Validate, Status::Failed,
             "ConsoleTarget.json framebuffer.bpp is " + std::to_string(bpp) + ", the PSP GU framebuffer is 16-bit (5650)");
        return false;
      }
    } catch (const std::exception& e) {
      note(sink, Stage::Validate, Status::Failed, e.what());
      return false;
    }
    return true;
  }

  // The PRX (release only) and the cooked pack next to the ELF. Dev builds boot the ELF with host0: = the build dir and the game reads
  // game.rpak from there; the file is a link to Library/Cooked/psp/game.rpak, so a re-cook is seen without copying (F5.21). A copy
  // is the fallback where links are not allowed. Release packaging wants a real file.
  bool placeOutputs(const BuildContext& ctx, const fs::path& cmakeElf, const fs::path& elfOut, EventSink& sink) {
    std::error_code ec;
    if (ctx.release) {
      fs::copy_file(cmakeElf.string() + ".prx", ctx.buildDir / "rensga_game.prx", fs::copy_options::overwrite_existing, ec);
      if (ec) {
        note(sink, Stage::Compile, Status::Failed, "missing PRX output " + cmakeElf.string() + ".prx");
        return false;
      }
    }
    (void)elfOut;
    const fs::path dst = ctx.buildDir / "game.rpak";
    const fs::path src = fs::absolute(ctx.cookedPack, ec);
    if (!ctx.release) {
      if (fs::is_symlink(dst, ec) && fs::read_symlink(dst, ec) == src && fs::exists(src, ec)) return true;
      fs::remove(dst, ec);
      fs::create_symlink(src, dst, ec);
      if (!ec) return true;
    }
    fs::remove(dst, ec);
    fs::copy_file(ctx.cookedPack, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      note(sink, Stage::Compile, Status::Failed, "cannot copy cooked pack " + ctx.cookedPack.string());
      return false;
    }
    return true;
  }

  bool compile(const BuildContext& ctx, EventSink sink) override {
    pipeline::ToolchainRunner tr = makeRunner();
    if (!resolveToolchain(tr, Stage::Compile, sink)) return false;
    // Native and Docker builds keep separate CMake trees (generator and toolchain paths differ).
    const bool native = tr.backend() == pipeline::ToolchainBackend::Native;
    fs::path cmakeDir = ctx.buildDir.parent_path() / ((native ? "cmake-native-" : "cmake-") + ctx.buildDir.filename().string());
    std::error_code ec;
    fs::create_directories(ctx.buildDir, ec);
    const fs::path elfOut = ctx.buildDir / "rensga_game.elf";

    // F5.21: everything the compiled game depends on. When none of it changed since the last good compile, the ELF is reused as it is
    // (no reflection scan, no CMake, no ninja). Scripts are hashed by content, the engine tree by name + size + mtime.
    pipeline::InputHasher in;
    in.addString(ctx.release ? "release" : "dev");
    in.addString(pipeline::toolchainBackendName(tr.backend()));
    in.addString(exitAfterFrames());
    in.addString(ctx.development ? "prof-stream" : "no-prof-stream");  // F6.06
    in.addString(extraCmakeArgs());  // F6.05: RENSGA_PSP_CMAKE_ARGS (CI: -DRENSGA_GU_DUMP_FRAME=<n> for screenshots)
    in.addFileContent(ctx.generatedDir / "console_target.gen.h", "console_target.gen.h");
    in.addFileContent(ctx.project / "ProjectSettings" / "ConsoleTarget.json", "ConsoleTarget.json");
    const std::vector<std::string> scripts = in.addTreeContent(ctx.project / "Assets", {".cpp", ".h", ".hpp", ".cc"});
    for (const char* dir : {"engine", "cmake", "tools/reflect"}) in.addTreeStat(repoRoot() / dir, {".cpp", ".c", ".h", ".hpp", ".txt", ".cmake", ".py", ".in", ".sh"});
    in.addFileContent(repoRoot() / "CMakeLists.txt", "CMakeLists.txt");
    in.addFileContent(repoRoot() / "CMakePresets.json", "CMakePresets.json");
    const std::string fingerprint = in.hex();
    const fs::path stamp = ctx.buildDir.parent_path() / (cmakeDir.filename().string() + ".compile-stamp");
    if (fs::exists(elfOut, ec) && pipeline::stampMatches(stamp, fingerprint)) {
      note(sink, Stage::Compile, Status::Progress, "compile skipped: no script, engine or build option changed since the last build");
      return placeOutputs(ctx, cmakeDir / "engine" / "runtime" / "rensga_game.elf", elfOut, sink);   // the PRX is next to the CMake tree's ELF
    }

    note(sink, Stage::Compile, Status::Progress, "reflection codegen");
    if (!prescanReflection(ctx, sink, cmakeDir / "reflect")) return false;
    std::vector<std::string> cfg = {"cmake", "--preset", "psp", "-B", cmakeDir.string(),
                                    "-DRENSGA_REFLECT_DIR=" + (cmakeDir / "reflect").string(),
                                    "-DRENSGA_PROJECT_DIR=" + ctx.project.string(),
                                    "-DRENSGA_GENERATED_DIR=" + ctx.generatedDir.string(),
                                    std::string("-DRENSGA_PSP_PRX=") + (ctx.release ? "ON" : "OFF"),
                                    // CI only (tools/ci/psp_eboot.sh): RENSGA_PSP_EXIT_AFTER_FRAMES=N makes the game quit cleanly after N
                                    // frames so a headless emulator run ends. Always passed (0 = never) so the cache never keeps a stale value.
                                    "-DRENSGA_EXIT_AFTER_FRAMES=" + exitAfterFrames(),
                                    // F6.06: development builds log one 'rensga: pf' profiler line per frame (the editor's Profiler reads them)
                                    std::string("-DRENSGA_PROF_STREAM_LOG=") + (ctx.development ? "ON" : "OFF")};
    cfg.push_back("-DRENSGA_GU_DUMP_FRAME=0");  // reset every run; RENSGA_PSP_CMAKE_ARGS below may set it
    {  // F6.05: extra cache entries for CI and screenshots, e.g. RENSGA_PSP_CMAKE_ARGS="-DRENSGA_GU_DUMP_FRAME=300"
      std::istringstream extra(extraCmakeArgs());
      for (std::string t; extra >> t;) cfg.push_back(t);
    }
    if (native) cfg.insert(cfg.begin() + 5, {"-G", "Ninja"});
    // CMake only needs to run again when its arguments or the set of script files changed; edits to a script are picked up by the build.
    pipeline::InputHasher ch;
    for (const std::string& c : cfg) ch.addString(c);
    for (const std::string& n : scripts) ch.addString(n);
    const fs::path cfgStamp = ctx.buildDir.parent_path() / (cmakeDir.filename().string() + ".configure-stamp");
    const bool treeExists = fs::exists(cmakeDir / "build.ninja", ec) || fs::exists(cmakeDir / "Makefile", ec);
    if (treeExists && pipeline::stampMatches(cfgStamp, ch.hex())) {
      note(sink, Stage::Compile, Status::Progress, "configure skipped (" + cmakeDir.string() + " is up to date)");
    } else {
      note(sink, Stage::Compile, Status::Progress, "configuring psp preset (" + cmakeDir.string() + ")");
      if (!toolRun(tr, ctx, Stage::Compile, sink, cfg)) return false;
      pipeline::writeStamp(cfgStamp, ch.hex());
    }
    note(sink, Stage::Compile, Status::Progress, "building rensga_game");
    if (!toolRun(tr, ctx, Stage::Compile, sink, {"cmake", "--build", cmakeDir.string(), "--target", "rensga_game", "-j" + std::to_string(buildJobs())}))
      return false;

    fs::path elf = cmakeDir / "engine" / "runtime" / "rensga_game.elf";
    fs::copy_file(elf, elfOut, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      note(sink, Stage::Compile, Status::Failed, "missing build output " + elf.string());
      return false;
    }
    if (!placeOutputs(ctx, elf, elfOut, sink)) return false;
    pipeline::writeStamp(stamp, fingerprint);
    return true;
  }

  // F5.03-F5.05: PARAM.SFO (title, disc ID, version from the project), ICON0 / PIC1 (the project's or placeholders made from the
  // brand tokens by tools/psp/make_package_art.py), optional SND0.AT3 (the project's; there is no AT3 encoder in the module), then
  // pack-pbp. Next to EBOOT.PBP the build dir gets game.rpak, every streamed music*.wav and LICENSES/.
  bool package(const BuildContext& ctx, EventSink sink) override {
    std::vector<std::string> warns;
    PackageSettings ps = loadPackageSettings(ctx.project, &warns);
    for (const std::string& w : warns) note(sink, Stage::Package, Status::Progress, "warning: " + w);
    pipeline::ToolchainRunner tr = makeRunner();
    if (!resolveToolchain(tr, Stage::Package, sink)) return false;
    std::error_code ec;
    const fs::path artDir = ctx.buildDir / "package";
    fs::create_directories(artDir, ec);

    // Art: the project's own files win; a missing ICON0 / PIC1 is made once per build.
    fs::path icon = ps.icon0, pic = ps.pic1;
    if (icon.empty() || pic.empty()) {
      ProcessSpec gen;
#ifdef _WIN32
      gen.argv = {"python", "-I", (repoRoot() / "tools" / "psp" / "make_package_art.py").string(), "--title", ps.title, "--out", artDir.string()};
#else
      gen.argv = {"python3", "-I", (repoRoot() / "tools" / "psp" / "make_package_art.py").string(), "--title", ps.title, "--out", artDir.string()};
#endif
      gen.cwd = repoRoot();
      int rc = pipeline::runProcess(gen, [&](const std::string& l) { note(sink, Stage::Package, Status::Progress, l); }, ctx.cancel);
      if (rc == 0) {
        if (icon.empty()) icon = artDir / "ICON0.png";
        if (pic.empty()) pic = artDir / "PIC1.png";
        note(sink, Stage::Package, Status::Progress, "package art: placeholder " + std::string(ps.icon0.empty() ? "ICON0 " : "") + (ps.pic1.empty() ? "PIC1 " : "") +
                                                      "made from the project name (put your own in ProjectSettings/Package/)");
      } else {
        note(sink, Stage::Package, Status::Progress, "warning: python3 is not available, using the bundled ICON0 and no PIC1");
        if (icon.empty()) icon = fs::path(RENSGA_PSP_ASSETS_DIR) / "ICON0.png";
      }
    }
    for (const auto& [what, file, w, h] : {std::tuple<const char*, fs::path, uint32_t, uint32_t>{"ICON0.png", icon, 144, 80}, {"PIC1.png", pic, 480, 272}}) {
      uint32_t pw = 0, ph = 0;
      if (!file.empty() && pngSize(file, pw, ph) && (pw != w || ph != h))
        note(sink, Stage::Package, Status::Progress, std::string("warning: ") + what + " is " + std::to_string(pw) + "x" + std::to_string(ph) + ", the XMB expects " + std::to_string(w) + "x" + std::to_string(h));
    }

    const fs::path sfo = ctx.buildDir / "PARAM.SFO";
    if (!toolRun(tr, ctx, Stage::Package, sink,
                 {"mksfoex", "-d", "MEMSIZE=1", "-s", "DISC_ID=" + ps.discId, "-s", "DISC_VERSION=" + ps.version, "-s", "APP_VER=" + ps.appVer, ps.title, sfo.string()}))
      return false;
    const std::string snd = ps.snd0.empty() ? "NULL" : ps.snd0.string();
    std::vector<std::string> pack = {"pack-pbp", (ctx.buildDir / "EBOOT.PBP").string(), sfo.string(), icon.string(), "NULL", "NULL",
                                     pic.empty() ? "NULL" : pic.string(), snd, (ctx.buildDir / "rensga_game.prx").string(), "NULL"};
    if (!toolRun(tr, ctx, Stage::Package, sink, pack)) return false;
    if (!fs::exists(ctx.buildDir / "EBOOT.PBP", ec)) return false;

    // The data that goes next to the EBOOT (ms0:/PSP/GAME/<folder>/): the pack is already there, add streamed audio and the licenses.
    int tracks = 0;
    for (const fs::path& w : streamedAudio(ctx.project, ctx.cookedPack)) {
      fs::copy_file(w, ctx.buildDir / w.filename(), fs::copy_options::overwrite_existing, ec);
      if (ec) { note(sink, Stage::Package, Status::Failed, "cannot copy " + w.string()); return false; }
      ++tracks;
    }
    std::string err;
    const auto lic = writeLicenseFiles(ctx.buildDir / "LICENSES", repoRoot(), tr.manager().root(kModuleId), err);
    if (!err.empty()) { note(sink, Stage::Package, Status::Failed, err); return false; }
    note(sink, Stage::Package, Status::Progress,
         "package: " + ps.title + " [" + ps.discId + " v" + ps.version + "], " + std::to_string(tracks) + " music file(s), " + std::to_string(lic.size()) + " license file(s) in " +
             (ctx.buildDir / "LICENSES").string());
    return true;
  }

  // F5.04: what the editor shows in Window > Emulators and uses for the install offer.
  ToolchainInfo toolchain() const override {
    ToolchainInfo t;
    t.moduleId = kModuleId;
    t.installed = makeRunner().manager().isInstalled(kModuleId);
    return t;
  }
  EmulatorInfo emulator() const override {
    EmulatorInfo e;
    e.path = PpssppLauncher::findExecutable();
    if (!e.path.empty()) e.version = PpssppLauncher::installedVersion(e.path.string());
    e.resetSandbox = [](const fs::path& project) {
      std::error_code ec;
      const fs::path sb = project / "Library" / "Emulators" / "ppsspp";
      if (fs::is_symlink(sb, ec)) { fs::remove(sb, ec); return !ec; }
      fs::remove_all(sb, ec);
      return !ec;
    };
    return e;
  }

  pipeline::EmulatorLauncher& launcher() override { return launcher_; }

 private:
  struct NoPlugin : ICookerFormatPlugin {
    std::string_view id() const override { return "none"; }
    bool cookMesh(const PrimitiveMesh&, const BakedLight&, Color32, std::vector<uint8_t>&, uint32_t&,
                  std::string& error) override {
      error = "no cooker plugin registered for psp";
      return false;
    }
  };
  ReleaseAwareLauncher launcher_;
};

}  // namespace

RENSGA_REGISTER_TARGET(PspTargetModule);

}  // namespace editor
}  // namespace rensga
