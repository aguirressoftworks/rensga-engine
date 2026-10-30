// PSP target module: cooker plugin lookup, Docker (pspdev) compile, EBOOT packaging, PPSSPP launcher.
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"
#include "rensga/editor/target_module.h"
#include "rensga/pipeline/process.h"
#include "ppsspp_launcher.h"

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

// Runs a command in the pspdev Docker image (tools/docker/psp.sh), streaming output as Progress events.
bool dockerRun(const BuildContext& ctx, Stage stage, const EventSink& sink, std::vector<std::string> cmd) {
  ProcessSpec ps;
  ps.argv.push_back((repoRoot() / "tools" / "docker" / "psp.sh").string());
  for (auto& c : cmd) ps.argv.push_back(std::move(c));
  ps.cwd = repoRoot();
  ps.env.emplace_back("RENSGA_PROJECT_DIR", ctx.project.string());
  std::string lastLine;
  int rc = pipeline::runProcess(
      ps, [&](const std::string& l) { lastLine = l; note(sink, stage, Status::Progress, l); }, ctx.cancel);
  if (rc != 0) note(sink, stage, Status::Failed, "command failed (exit " + std::to_string(rc) + "): " + ps.argv[1] + " " + (ps.argv.size() > 2 ? ps.argv[2] : ""));
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

class PspTargetModule : public TargetModule {
 public:
  std::string_view id() const override { return "psp"; }
  std::string_view displayName() const override { return "PSP"; }

  ICookerFormatPlugin& cookerPlugin() override {
    ICookerFormatPlugin* p = CookerPluginRegistry::instance().find("psp");
    static NoPlugin none;
    return p ? *p : none;
  }

  // F3.02: the PSP GU keeps its buffers at 512-pixel strides (draw buffer, display buffer, depth buffer, in that order);
  // textures follow, rows padded to 16 bytes, each block 16-byte aligned. Palettes are 16-byte aligned too.
  VramLayout vramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items) const override {
    VramLayout l;
    l.capacity = ct.vramBytes;
    uint64_t at = 0;
    for (const VramItem& src : items) {
      VramItem it = src;
      uint64_t size = it.bytes;
      if (it.kind == "framebuffer" || it.kind == "depth") {
        size = 512ull * it.height * it.bits / 8;  // fixed 512-pixel stride
      } else if (it.kind == "texture" && it.width && it.height && it.bits) {
        const uint64_t pitch = (static_cast<uint64_t>(it.width) * it.bits / 8 + 15) / 16 * 16;
        size = pitch * it.height;
      }
      at = (at + 15) / 16 * 16;
      VramPlacement p;
      p.item = it;
      p.offset = at;
      p.size = size;
      at += size;
      p.outside = at > l.capacity;
      l.items.push_back(std::move(p));
    }
    l.used = at;
    return l;
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
    } catch (const std::exception& e) {
      note(sink, Stage::Validate, Status::Failed, e.what());
      return false;
    }
    return true;
  }

  bool compile(const BuildContext& ctx, EventSink sink) override {
    fs::path cmakeDir = ctx.buildDir.parent_path() / ("cmake-" + ctx.buildDir.filename().string());
    note(sink, Stage::Compile, Status::Progress, "configuring psp preset in Docker (" + cmakeDir.string() + ")");
    note(sink, Stage::Compile, Status::Progress, "reflection codegen");
    if (!prescanReflection(ctx, sink, cmakeDir / "reflect")) return false;
    std::vector<std::string> cfg = {"cmake", "--preset", "psp", "-B", cmakeDir.string(),
                                    "-DRENSGA_REFLECT_DIR=" + (cmakeDir / "reflect").string(),
                                    "-DRENSGA_PROJECT_DIR=" + ctx.project.string(),
                                    "-DRENSGA_GENERATED_DIR=" + ctx.generatedDir.string(),
                                    std::string("-DRENSGA_PSP_PRX=") + (ctx.release ? "ON" : "OFF")};
    if (!dockerRun(ctx, Stage::Compile, sink, cfg)) return false;
    note(sink, Stage::Compile, Status::Progress, "building rensga_game");
    if (!dockerRun(ctx, Stage::Compile, sink, {"cmake", "--build", cmakeDir.string(), "--target", "rensga_game", "-j4"}))
      return false;

    std::error_code ec;
    fs::path elf = cmakeDir / "engine" / "runtime" / "rensga_game.elf";
    fs::create_directories(ctx.buildDir, ec);
    fs::copy_file(elf, ctx.buildDir / "rensga_game.elf", fs::copy_options::overwrite_existing, ec);
    if (ec) {
      note(sink, Stage::Compile, Status::Failed, "missing build output " + elf.string());
      return false;
    }
    if (ctx.release) {
      fs::copy_file(elf.string() + ".prx", ctx.buildDir / "rensga_game.prx", fs::copy_options::overwrite_existing, ec);
      if (ec) {
        note(sink, Stage::Compile, Status::Failed, "missing PRX output " + elf.string() + ".prx");
        return false;
      }
    }
    fs::copy_file(ctx.cookedPack, ctx.buildDir / "game.rpak", fs::copy_options::overwrite_existing, ec);
    if (ec) {
      note(sink, Stage::Compile, Status::Failed, "cannot copy cooked pack " + ctx.cookedPack.string());
      return false;
    }
    return true;
  }

  bool package(const BuildContext& ctx, EventSink sink) override {
    std::string title = "Rensga Game";
    try {
      std::ifstream f(ctx.project / "ProjectSettings" / "Project.json");
      title = nlohmann::json::parse(f).value("name", title);
    } catch (...) {
    }
    fs::path icon = fs::path(RENSGA_PSP_ASSETS_DIR) / "ICON0.png";
    fs::path sfo = ctx.buildDir / "PARAM.SFO";
    if (!dockerRun(ctx, Stage::Package, sink, {"mksfoex", "-d", "MEMSIZE=1", title, sfo.string()})) return false;
    std::vector<std::string> pack = {"pack-pbp", (ctx.buildDir / "EBOOT.PBP").string(), sfo.string(), icon.string(),
                                     "NULL", "NULL", "NULL", "NULL", (ctx.buildDir / "rensga_game.prx").string(), "NULL"};
    if (!dockerRun(ctx, Stage::Package, sink, pack)) return false;
    return fs::exists(ctx.buildDir / "EBOOT.PBP");
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
  PpssppLauncher launcher_;
};

}  // namespace

RENSGA_REGISTER_TARGET(PspTargetModule);

}  // namespace editor
}  // namespace rensga
