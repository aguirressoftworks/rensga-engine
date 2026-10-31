#include "ppsspp_launcher.h"

#include <signal.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using pipeline::Event;
using pipeline::ExitState;
using pipeline::Stage;
using pipeline::Status;

std::string PpssppLauncher::findExecutable() {
  if (const char* e = std::getenv("RENSGA_PPSSPP"); e && *e) return e;
  std::error_code ec;
  // The "ppsspp" emulator module (Windows download) in the modules dir.
  {
    fs::path mods;
    if (const char* h = std::getenv("RENSGA_HOME"); h && *h) mods = fs::path(h) / "modules";
    else if (const char* la = std::getenv("LOCALAPPDATA"); la && *la) mods = fs::path(la) / "Rensga" / "modules";
    else if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) mods = fs::path(x) / "rensga" / "modules";
    else if (const char* home = std::getenv("HOME"); home && *home) mods = fs::path(home) / ".local" / "share" / "rensga" / "modules";
    for (fs::directory_iterator it(mods, ec), end; !ec && it != end; it.increment(ec)) {
      const std::string n = it->path().filename().string();
      if (n.rfind("ppsspp-", 0) != 0) continue;
      for (const char* exe : {"PPSSPPWindows64.exe", "PPSSPPSDL"})
        if (fs::exists(it->path() / exe, ec)) return (it->path() / exe).string();
    }
  }
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
  // Flatpak export (the Linux "manual" install of the ppsspp module).
  fs::path fp = "/var/lib/flatpak/exports/bin/org.ppsspp.PPSSPP";
  if (fs::exists(fp, ec)) return fp.string();
  return {};
}

std::string PpssppLauncher::findHeadless(const std::string& gui) {
  if (const char* e = std::getenv("RENSGA_PPSSPP_HEADLESS"); e && *e) return e;
  std::error_code ec;
  if (!gui.empty()) {
    for (const char* name : {"PPSSPPHeadless", "PPSSPPHeadless.exe"}) {
      fs::path c = fs::path(gui).parent_path() / name;
      if (fs::exists(c, ec)) return c.string();
    }
  }
  if (const char* path = std::getenv("PATH")) {
    std::string p = path;
    size_t pos = 0;
    while (pos <= p.size()) {
      size_t end = p.find(':', pos);
      if (end == std::string::npos) end = p.size();
      fs::path c = fs::path(p.substr(pos, end - pos)) / "PPSSPPHeadless";
      if (end > pos && fs::exists(c, ec)) return c.string();
      pos = end + 1;
    }
  }
  return {};
}

std::string PpssppLauncher::pinnedVersion() {
  std::ifstream f(fs::path(RENSGA_SOURCE_DIR) / "third_party" / "emulators" / "ppsspp" / "VERSION");
  std::string v;
  if (!(f >> v)) return "1.20.4";
  const size_t dash = v.find('-');  // "1.20.4-1" is version + packaging revision
  return dash == std::string::npos ? v : v.substr(0, dash);
}

std::string PpssppLauncher::installedVersion(const std::string& exe) {
  static std::mutex m;
  static std::map<std::string, std::string> cache;
  {
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(exe);
    if (it != cache.end()) return it->second;
  }
  std::string out;
  pipeline::Process p;
  pipeline::ProcessSpec ps;
  ps.argv = {exe, "--version"};
  ps.env.emplace_back("SDL_VIDEODRIVER", "dummy");
  if (p.start(ps, [&](const std::string& l) {
        if (out.empty() && !l.empty() && (l[0] == 'v' || (l[0] >= '0' && l[0] <= '9'))) out = l;
      })) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (p.running() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (p.running()) p.terminate(200);
    p.wait();
  }
  if (!out.empty() && out[0] == 'v') out.erase(0, 1);
  const size_t sp = out.find_first_of(" -\t");  // "1.20.4-123-gabcdef" -> "1.20.4"
  if (sp != std::string::npos) out.resize(sp);
  std::lock_guard<std::mutex> lock(m);
  cache[exe] = out;
  return out;
}

namespace {

// Sets `key = value` in [section] of a ppsspp.ini, keeping every other line. `onlyIfMissing`: leave an existing value alone.
void iniSet(const fs::path& path, const std::string& section, const std::string& key, const std::string& value, bool onlyIfMissing) {
  std::vector<std::string> lines;
  {
    std::ifstream f(path);
    for (std::string l; std::getline(f, l);) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      lines.push_back(l);
    }
  }
  const std::string head = "[" + section + "]";
  size_t secAt = lines.size(), end = lines.size();
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i] == head) {
      secAt = i;
      end = lines.size();
      for (size_t j = i + 1; j < lines.size(); ++j)
        if (!lines[j].empty() && lines[j][0] == '[') { end = j; break; }
      break;
    }
  }
  if (secAt == lines.size()) {
    lines.push_back(head);
    lines.push_back(key + " = " + value);
  } else {
    bool found = false;
    for (size_t i = secAt + 1; i < end; ++i) {
      size_t eq = lines[i].find('=');
      if (eq == std::string::npos) continue;
      std::string k = lines[i].substr(0, eq);
      while (!k.empty() && k.back() == ' ') k.pop_back();
      if (k != key) continue;
      found = true;
      if (!onlyIfMissing) lines[i] = key + " = " + value;
    }
    if (!found) lines.insert(lines.begin() + static_cast<long>(end), key + " = " + value);
  }
  std::ofstream f(path, std::ios::trunc);
  for (const std::string& l : lines) f << l << "\n";
}

// The start of the game's own text in a line: PSP lines look like "INFO [game] rensga: ...", possibly behind the emulator's
// timestamp / log prefix. Returns npos for lines that are not the game's.
size_t gameTextAt(const std::string& l) {
  size_t best = std::string::npos;
  for (const char* tok : {"ERROR [", "WARN [", "INFO [", "DEBUG ["}) {
    size_t p = l.find(tok);
    if (p != std::string::npos && p < best) best = p;
  }
  if (best != std::string::npos && l.find("rensga", best) != std::string::npos) return best;
  size_t r = l.find("rensga:");
  return r;
}

const char* signalName(int s) {
  switch (s) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGKILL: return "SIGKILL";
    case SIGTERM: return "SIGTERM";
    case SIGHUP: return "SIGHUP";
    case SIGINT: return "SIGINT";
    default: return "signal";
  }
}

}  // namespace

long PpssppLauncher::nowMs() const {
  return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count());
}

void PpssppLauncher::note(const std::string& line) {
  std::lock_guard<std::mutex> lock(mu_);
  log_.push_back(line);
  all_.push_back(line);
}

void PpssppLauncher::flushRepeat() {
  int n;
  {
    std::lock_guard<std::mutex> lock(mu_);
    n = emuRepeat_;
    emuRepeat_ = 0;
    lastEmu_.clear();
  }
  if (n > 0) note("[ppsspp] (the line above repeated " + std::to_string(n) + " more times)");
}

void PpssppLauncher::ingest(const std::string& line, int source) {
  if (line.empty()) return;
  const size_t at = gameTextAt(line);
  if (at == std::string::npos) {
    // Not a game line: keep emulator errors (a guest crash is reported as one), drop its boot chatter and warnings.
    if (source == 0 && line.find(" E[") != std::string::npos && line.find("ISO looks bogus") == std::string::npos) {
      // PPSSPP logs a guest CPU exception as "E[CPU]: Core/Core.cpp:628 BREAK!" (probed with 1.20.4) and then stops the game
      // while its window stays open; other releases word it as an exception or a bad memory access.
      if (line.find("E[CPU]") != std::string::npos || line.find("BREAK!") != std::string::npos ||
          line.find("xception") != std::string::npos || line.find("Game crashed") != std::string::npos ||
          line.find("Bad memory") != std::string::npos)
        crashedByLog_ = true;
      // The same emulator error every frame (an unimplemented syscall) would drown the Console: show it once, then count.
      const size_t colon = line.find("]: ");
      const std::string key = colon == std::string::npos ? line : line.substr(colon + 3);
      bool repeat;
      {
        std::lock_guard<std::mutex> lock(mu_);
        repeat = key == lastEmu_;
        if (repeat) ++emuRepeat_;
      }
      if (repeat) return;
      flushRepeat();
      { std::lock_guard<std::mutex> lock(mu_); lastEmu_ = key; }
      note("[emu] " + line);
    }
    return;
  }
  std::string norm = line.substr(at);
  if (norm.find("rensga:") != std::string::npos) {
    if (!sawGame_.exchange(true)) lastBeatMs_ = nowMs();
    if (norm.find("rensga: frame ") != std::string::npos) lastBeatMs_ = nowMs();
    if (norm.find("devctl paused") != std::string::npos) ackedPause_ = true;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto& other = pending_[1 - source];
    auto it = other.find(norm);
    if (it != other.end() && it->second > 0) {  // the same line already arrived through the other source
      if (--it->second == 0) other.erase(it);
      return;
    }
    if (pending_[0].size() + pending_[1].size() > 512) { pending_[0].clear(); pending_[1].clear(); }
    ++pending_[source][norm];
    log_.push_back(norm);
    all_.push_back(norm);
  }
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
  auto progress = [&](const std::string& m) {
    if (!sink) return;
    Event e;
    e.stage = Stage::Launch;
    e.status = Status::Progress;
    e.message = m;
    sink(e);
  };
  std::string gui = findExecutable();
  std::string exe = gui;
  if (spec.headless) {
    exe = findHeadless(gui);
    if (exe.empty()) return fail("PPSSPPHeadless not found next to the PPSSPP executable (headless run needs it; set RENSGA_PPSSPP_HEADLESS)");
  } else if (exe.empty()) {
    return fail("PPSSPP not found (install ppsspp, run tools/emulators/fetch_ppsspp.sh, or set RENSGA_PPSSPP)");
  }
  fs::path elf = spec.executable.empty() ? spec.workingDir / "rensga_game.elf" : spec.executable;
  if (!fs::exists(elf)) return fail("game image not found: " + elf.string());

  // F5.19 version check: the editor is tested against one PPSSPP release; others may behave differently.
  {
    const std::string have = installedVersion(exe), pin = pinnedVersion();
    if (have.empty()) progress("PPSSPP version could not be read; Rensga is tested with " + pin);
    else if (have != pin) progress("warning: PPSSPP " + have + " differs from the pinned " + pin + "; Play may behave differently (install the pinned version from Hub > Modules)");
    else progress("PPSSPP " + have + " (pinned)");
  }

  // Per-project sandbox. The memory stick (saves, config) is <configDir>/ppsspp, or LaunchSpec::memstickDir through a link.
  std::error_code ec;
  fs::path cfgRoot = spec.configDir / "ppsspp";
  if (!spec.memstickDir.empty() && spec.memstickDir != cfgRoot) {
    fs::create_directories(spec.memstickDir, ec);
    fs::remove(cfgRoot, ec);  // only removes an empty directory or a link
    fs::create_directory_symlink(fs::absolute(spec.memstickDir, ec), cfgRoot, ec);
    if (ec && !fs::exists(cfgRoot)) return fail("cannot place the emulator memory stick at " + spec.memstickDir.string());
  }
  fs::path ini = cfgRoot / "PSP" / "SYSTEM" / "ppsspp.ini";
  fs::create_directories(ini.parent_path(), ec);
  // First-run prompts off (only when the key is absent, so a test or user can set their own); the debugger flag is ours every launch.
  iniSet(ini, "General", "FirstRun", "False", true);
  iniSet(ini, "General", "CheckForNewVersion", "False", true);
  iniSet(ini, "General", "EnableLogging", "False", true);
  iniSet(ini, "Graphics", "FullScreen", "False", true);
  iniSet(ini, "General", "RemoteDebuggerOnStartup", spec.debugger ? "True" : "False", false);
  // Key bindings are project data: ProjectSettings/Emulators/ppsspp/controls.ini replaces the sandbox copy
  // on every launch (configDir is <project>/Library/Emulators).
  fs::path bindings = spec.configDir.parent_path().parent_path() / "ProjectSettings" / "Emulators" / "ppsspp" / "controls.ini";
  if (fs::exists(bindings)) fs::copy_file(bindings, ini.parent_path() / "controls.ini", fs::copy_options::overwrite_existing, ec);

  fs::path logFile = spec.workingDir / "rensga.log";
  ctlPath_ = spec.workingDir / "rensga.ctl";
  fs::remove(logFile, ec);
  fs::remove(ctlPath_, ec);
  {
    std::lock_guard<std::mutex> lock(mu_);
    log_.clear();
    all_.clear();
    detail_.clear();
    lastEmu_.clear();
    emuRepeat_ = 0;
    pending_[0].clear();
    pending_[1].clear();
  }
  cmds_.clear();
  seq_ = 0;
  spec_ = spec;
  t0_ = std::chrono::steady_clock::now();
  stopFlag_ = false;
  stopRequested_ = false;
  paused_ = false;
  sigStopped_ = false;
  ackedPause_ = false;
  pauseDeadlineMs_ = 0;
  lastBeatMs_ = 0;
  sawGame_ = false;
  hung_ = false;
  crashedByLog_ = false;
  ended_ = false;
  exitCode_ = -1;
  exitSignal_ = 0;

  pipeline::ProcessSpec ps;
  if (spec.headless) {
    const char* gfx = std::getenv("RENSGA_PPSSPP_GFX");
    const int timeout = spec.quitAfterSeconds > 0 ? spec.quitAfterSeconds : 3600;
    ps.argv = {exe, elf.string(), "-r", spec.workingDir.string(), "-l", std::string("--graphics=") + (gfx && *gfx ? gfx : "software"),
               "--timeout=" + std::to_string(timeout)};
    if (spec.debugger) ps.argv.push_back("--debugger=0");
  } else {
    ps.argv = {exe, elf.string()};
  }
  ps.cwd = spec.workingDir;
  ps.env.emplace_back("XDG_CONFIG_HOME", spec.configDir.string());
  ps.env.emplace_back("SDL_AUDIODRIVER", "dummy");
  ps.env.emplace_back("RENSGA_DEVCTL", ctlPath_.string());  // desktop-style hosts honour it; on PSP the game opens host0:/rensga.ctl
  for (const auto& kv : spec.env) ps.env.push_back(kv);
  if (!proc_.start(ps, [this](const std::string& l) { ingest(l, 0); })) return fail("could not start " + exe);
  tailActive_ = true;
  tail_ = std::thread(&PpssppLauncher::tailLoop, this, logFile);
  return true;
}

void PpssppLauncher::tailLoop(fs::path logFile) {
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
        if (!partial.empty()) ingest(partial, 1);
        partial.clear();
      } else if (c != '\r') {
        partial.push_back(c);
      }
    }
  };
  bool quitSent = false;
  while (!stopFlag_) {
    const bool alive = proc_.running();
    readNew();
    if (!alive) break;
    const long now = nowMs();
    if (spec_.quitAfterSeconds > 0 && !quitSent && now > spec_.quitAfterSeconds * 1000L) {
      quitSent = true;
      stopRequested_ = true;
      note("rensga: quit-after reached, stopping emulator");
      std::lock_guard<std::mutex> lock(ctl_);
      proc_.terminate();
    }
    // Pause without an answer (a game without the control channel): freeze the emulator process instead.
    const long dl = pauseDeadlineMs_.load();
    if (dl && now > dl) {
      pauseDeadlineMs_ = 0;
      if (!ackedPause_ && paused_) {
        std::lock_guard<std::mutex> lock(ctl_);
        if (proc_.signalGroup(SIGSTOP)) {
          sigStopped_ = true;
          note("[launcher] the game did not answer the pause command; froze the emulator process (SIGSTOP)");
        }
      }
    }
    // Hang: alive, not frozen on purpose, and the heartbeat stopped.
    if (!hung_ && !sigStopped_ && !crashedByLog_) {
      const bool beat = sawGame_;
      const long since = beat ? now - lastBeatMs_.load() : now;
      const int limit = beat ? spec_.hangTimeoutSeconds : spec_.bootTimeoutSeconds;
      if (limit > 0 && since > limit * 1000L) {
        hung_ = true;
        std::ostringstream o;
        if (beat) o << "hung: no 'rensga: frame' heartbeat for " << since / 1000 << " s";
        else o << "hung: the game printed nothing within " << since / 1000 << " s of starting";
        { std::lock_guard<std::mutex> lock(mu_); detail_ = o.str(); }
        note("[launcher] " + o.str());
      }
    } else if (hung_ && sawGame_ && !sigStopped_ && nowMs() - lastBeatMs_.load() < 2000) {
      hung_ = false;  // it recovered (a long load, not a hang)
      note("[launcher] the game is running again");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  readNew();
  if (!partial.empty()) ingest(partial, 1);
  finish();
  tailActive_ = false;
}

void PpssppLauncher::finish() {
  // Collect the exit status (the process may already be gone, or be stopping because stop() was called).
  bool alive = proc_.running();
  for (int i = 0; alive && stopRequested_ && i < 150; ++i) {  // a stop is in progress: the child is on its way out
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    alive = proc_.running();
  }
  if (!alive) {
    exitSignal_ = proc_.exitSignal();
  }
  int code = -1;
  if (!alive) code = proc_.wait();
  exitCode_ = code;
  std::string d;
  if (!alive) {
    const int sig = exitSignal_.load();
    if (stopRequested_) d = spec_.quitAfterSeconds > 0 && nowMs() >= spec_.quitAfterSeconds * 1000L ? "stopped (quit-after reached)" : "stopped by the editor";
    else if (sig) d = std::string("crashed: the emulator was killed by signal ") + std::to_string(sig) + " (" + signalName(sig) + ")";
    else if (code != 0) d = "crashed: the emulator exited with code " + std::to_string(code);
    else if (crashedByLog_) d = "crashed: the game raised a CPU exception (see the emulator lines above)";
    else d = "exited normally";
  }
  if (!d.empty()) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      detail_ = d;
    }
    note("[launcher] " + d);
  }
  flushRepeat();
  ended_ = true;
  if (!spec_.logPath.empty()) saveLog(spec_.logPath);
}

ExitState PpssppLauncher::exitState() const {
  if (ended_) {
    if (stopRequested_) return ExitState::Exited;
    if (exitSignal_ != 0 || exitCode_.load() > 0 || crashedByLog_) return ExitState::Crashed;
    return ExitState::Exited;
  }
  if (!tailActive_) return ExitState::Exited;  // never started
  if (crashedByLog_ && sawGame_) return ExitState::Crashed;  // the emulator reported a guest exception and keeps its window open
  if (hung_) return ExitState::Hung;
  return ExitState::Running;
}

std::string PpssppLauncher::exitDetail() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!ended_ && crashedByLog_) return "crashed: the game raised a CPU exception (the emulator reported it and stopped the game)";
  return detail_;
}

bool PpssppLauncher::sendCtl(const std::string& command) {
  if (!running() || ended_) return false;
  std::lock_guard<std::mutex> lock(ctl_);
  cmds_.push_back(std::to_string(++seq_) + " " + command);
  while (cmds_.size() > 12) cmds_.erase(cmds_.begin());
  std::error_code ec;
  fs::path tmp = ctlPath_.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    for (const std::string& c : cmds_) f << c << "\n";
    if (!f) return false;
  }
  fs::rename(tmp, ctlPath_, ec);
  return !ec;
}

bool PpssppLauncher::pause() {
  if (paused_) return true;
  if (!sendCtl("pause")) return false;
  paused_ = true;
  ackedPause_ = false;
  pauseDeadlineMs_ = nowMs() + 3000;
  return true;
}

bool PpssppLauncher::resume() {
  if (!paused_) return true;
  if (sigStopped_) {
    std::lock_guard<std::mutex> lock(ctl_);
    proc_.signalGroup(SIGCONT);
    sigStopped_ = false;
    lastBeatMs_ = nowMs();
  }
  pauseDeadlineMs_ = 0;
  if (!sendCtl("resume")) return false;
  paused_ = false;
  return true;
}

bool PpssppLauncher::step(int n) {
  if (n < 1) n = 1;
  if (sigStopped_) return false;  // frame-exact stepping needs the game's control channel
  if (!sendCtl("step " + std::to_string(n))) return false;
  paused_ = true;
  ackedPause_ = true;  // the step command pauses the game itself
  pauseDeadlineMs_ = 0;
  return true;
}

void PpssppLauncher::stop() {
  if (!ended_ && (tailActive_ || proc_.running())) stopRequested_ = true;  // a session that already crashed keeps its verdict
  {
    std::lock_guard<std::mutex> lock(ctl_);
    if (sigStopped_) { proc_.signalGroup(SIGCONT); sigStopped_ = false; }
    proc_.terminate();  // first the process, then the tail thread: it classifies the end once the child is really gone
  }
  stopFlag_ = true;
  if (tail_.joinable() && tail_.get_id() != std::this_thread::get_id()) tail_.join();
  tailActive_ = false;
  paused_ = false;
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

bool PpssppLauncher::saveLog(const fs::path& path) const {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  std::lock_guard<std::mutex> lock(mu_);
  for (const std::string& l : all_) f << l << "\n";
  return static_cast<bool>(f);
}

}  // namespace editor
}  // namespace rensga
