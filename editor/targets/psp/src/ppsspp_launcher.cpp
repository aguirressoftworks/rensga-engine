#include "ppsspp_launcher.h"

#include <cstdlib>
#include <fstream>

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using pipeline::Event;
using pipeline::Stage;
using pipeline::Status;

std::string PpssppLauncher::findExecutable() {
  if (const char* e = std::getenv("RENSGA_PPSSPP"); e && *e) return e;
  std::error_code ec;
  fs::path vendored = fs::path(RENSGA_SOURCE_DIR) / "third_party" / "emulators" / "ppsspp" / "PPSSPPSDL";
  if (fs::exists(vendored, ec)) return vendored.string();
  if (const char* path = std::getenv("PATH")) {
    std::string p = path;
    size_t pos = 0;
    while (pos <= p.size()) {
      size_t end = p.find(':', pos);
      if (end == std::string::npos) end = p.size();
      for (const char* name : {"PPSSPPSDL", "ppsspp", "PPSSPPQt"}) {
        fs::path c = fs::path(p.substr(pos, end - pos)) / name;
        if (!p.substr(pos, end - pos).empty() && fs::exists(c, ec)) return c.string();
      }
      pos = end + 1;
    }
  }
  return {};
}

void PpssppLauncher::push(const std::string& line) {
  std::lock_guard<std::mutex> lock(mu_);
  log_.push_back(line);
}

bool PpssppLauncher::start(const pipeline::LaunchSpec& spec, pipeline::EventSink sink) {
  stop();
  auto fail = [&](const std::string& m) {
    if (sink) {
      Event e;
      e.stage = Stage::Launch;
      e.status = Status::Failed;
      e.message = m;
      sink(e);
    }
    return false;
  };
  std::string exe = findExecutable();
  if (exe.empty()) return fail("PPSSPP not found (install ppsspp, run tools/emulators/fetch_ppsspp.sh, or set RENSGA_PPSSPP)");
  fs::path elf = spec.executable.empty() ? spec.workingDir / "rensga_game.elf" : spec.executable;
  if (!fs::exists(elf)) return fail("game image not found: " + elf.string());

  // Per-project config; first-run prompts off. BIOS policy: none (PPSSPP HLE).
  std::error_code ec;
  fs::path cfgRoot = spec.configDir / "ppsspp";
  fs::path ini = cfgRoot / "PSP" / "SYSTEM" / "ppsspp.ini";
  fs::create_directories(ini.parent_path(), ec);
  if (!fs::exists(ini)) {
    std::ofstream(ini) << "[General]\nFirstRun = False\nCheckForNewVersion = False\nEnableLogging = False\n"
                          "[Graphics]\nFullScreen = False\n";
  }
  // Key bindings are project data: ProjectSettings/Emulators/ppsspp/controls.ini replaces the sandbox copy
  // on every launch (configDir is <project>/Library/Emulators).
  fs::path bindings = spec.configDir.parent_path().parent_path() / "ProjectSettings" / "Emulators" / "ppsspp" / "controls.ini";
  if (fs::exists(bindings)) fs::copy_file(bindings, ini.parent_path() / "controls.ini", fs::copy_options::overwrite_existing, ec);
  fs::path logFile = spec.workingDir / "rensga.log";
  fs::remove(logFile, ec);
  {
    std::lock_guard<std::mutex> lock(mu_);
    log_.clear();
  }

  pipeline::ProcessSpec ps;
  ps.argv = {exe, elf.string()};
  ps.cwd = spec.workingDir;
  ps.env.emplace_back("XDG_CONFIG_HOME", spec.configDir.string());
  ps.env.emplace_back("SDL_AUDIODRIVER", "dummy");
  // Keep the game's lines and emulator errors; drop the emulator's boot chatter and warnings.
  auto filter = [this](const std::string& l) {
    if (l.find("rensga") != std::string::npos ||
        (l.find(" E[") != std::string::npos && l.find("ISO looks bogus") == std::string::npos))
      push("[ppsspp] " + l);
  };
  stopFlag_ = false;
  if (!proc_.start(ps, filter)) return fail("could not start " + exe);
  tailActive_ = true;
  tail_ = std::thread(&PpssppLauncher::tailLoop, this, logFile, spec.quitAfterSeconds);
  return true;
}

void PpssppLauncher::tailLoop(fs::path logFile, int quitAfterSeconds) {
  auto begin = std::chrono::steady_clock::now();
  std::streamoff offset = 0;
  std::string partial;
  auto readNew = [&]() {
    std::ifstream f(logFile, std::ios::binary);
    if (!f) return;
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < offset) offset = 0;  // log was recreated
    f.seekg(offset);
    std::string chunk(static_cast<size_t>(size - offset), '\0');
    f.read(&chunk[0], static_cast<std::streamsize>(chunk.size()));
    offset = size;
    for (char c : chunk) {
      if (c == '\n') {
        if (!partial.empty()) push(partial);
        partial.clear();
      } else if (c != '\r') {
        partial.push_back(c);
      }
    }
  };
  bool killed = false;
  while (!stopFlag_) {
    bool alive = proc_.running();
    readNew();
    if (!alive) break;
    if (quitAfterSeconds > 0 && !killed &&
        std::chrono::steady_clock::now() - begin > std::chrono::seconds(quitAfterSeconds)) {
      killed = true;
      push("rensga: quit-after reached, stopping emulator");
      std::lock_guard<std::mutex> lock(ctl_);
      proc_.terminate();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  readNew();
  if (!partial.empty()) push(partial);
  tailActive_ = false;
}

void PpssppLauncher::stop() {
  stopFlag_ = true;
  {
    std::lock_guard<std::mutex> lock(ctl_);
    proc_.terminate();
  }
  if (tail_.joinable() && tail_.get_id() != std::this_thread::get_id()) tail_.join();
  tailActive_ = false;
}

bool PpssppLauncher::running() const {
  return const_cast<pipeline::Process&>(proc_).running() || tailActive_;
}

std::vector<std::string> PpssppLauncher::drainLog() {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> out;
  out.swap(log_);
  return out;
}

}  // namespace editor
}  // namespace rensga
