// F5.19-F5.22: launcher behaviour without a real emulator. A shell script plays the emulator (RENSGA_PPSSPP): it prints game lines to
// stdout and to host0:/rensga.log, and in other modes crashes, exits with a code, or stops printing. Checks line de-duplication,
// Stop, crash / hang classification, the pause fallback and last-play.log.
#include <signal.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "ppsspp_launcher.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;

namespace {

fs::path tmpDir(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / "emu_play" / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}

// The fake emulator. FAKE_MODE: run | crash | exit3 | hang | clean. `--version` prints $FAKE_VERSION.
fs::path fakeEmulator(const fs::path& dir, const char* file) {
  fs::path p = dir / file;
  {
    std::ofstream f(p);
    f << "#!/bin/sh\n"
         "if [ \"$1\" = \"--version\" ]; then echo \"${FAKE_VERSION:-v1.20.4}\"; exit 0; fi\n"
         "emit() { echo \"INFO [game] $1\"; echo \"INFO [game] $1\" >> rensga.log; }\n"
         "echo \"12:00:00.001 Thread/ThreadManager.cpp:227 I[SYSTEM]: boot chatter that must be dropped\"\n"
         "emit 'rensga: boot'\n"
         "n=1\n"
         "case \"$FAKE_MODE\" in\n"
         "  crash) emit 'rensga: frame 1'; sleep 0.3; kill -SEGV $$ ;;\n"
         "  exit3) emit 'rensga: frame 1'; exit 3 ;;\n"
         "  clean) emit 'rensga: frame 1'; exit 0 ;;\n"
         "  hang) emit 'rensga: frame 1'; sleep 60 ;;\n"
         "  *) while :; do emit \"rensga: frame $n\"; echo '12:00:00.002 HLE/HLE.cpp:969 E[HLE]: Unimplemented HLE function sceFake'; n=$((n+1)); sleep 0.1; done ;;\n"
         "esac\n";
  }
  chmod(p.c_str(), 0755);
  return p;
}

struct Session {
  PpssppLauncher l;
  std::vector<std::string> lines;
  std::vector<std::string> events;
  bool start(const fs::path& dir, const char* mode, int hang = 15, const char* exe = "fake") {
    setenv("RENSGA_PPSSPP", (dir / exe).c_str(), 1);
    pipeline::LaunchSpec spec;
    spec.workingDir = dir / "build";
    spec.configDir = dir / "Library" / "Emulators";
    spec.logPath = dir / "Library" / "Logs" / "last-play.log";
    spec.hangTimeoutSeconds = hang;
    spec.env.emplace_back("FAKE_MODE", mode);
    fs::create_directories(spec.workingDir);
    std::ofstream(spec.workingDir / "rensga_game.elf") << "ELF";
    return l.start(spec, [this](const pipeline::Event& e) { events.push_back(e.message); });
  }
  void drain() {
    for (auto& s : l.drainLog()) lines.push_back(s);
  }
  bool waitFor(const std::function<bool()>& cond, int ms) {
    for (int t = 0; t < ms; t += 20) {
      drain();
      if (cond()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    drain();
    return cond();
  }
  int count(const std::string& needle) const {
    int n = 0;
    for (auto& s : lines) n += s.find(needle) != std::string::npos;
    return n;
  }
};

}  // namespace

TEST(launcher_dedupes_stops_and_saves_log) {
  fs::path d = tmpDir("run");
  fakeEmulator(d, "fake");
  Session s;
  CHECK(s.start(d, "run"));
  CHECK(s.waitFor([&] { return s.count("rensga: frame 3") >= 1; }, 5000));
  CHECK(s.l.running());
  CHECK(s.l.exitState() == pipeline::ExitState::Running);
  s.l.stop();
  CHECK(!s.l.running());  // Stop leaves no process behind
  s.drain();
  CHECK(s.l.exitState() == pipeline::ExitState::Exited);
  CHECK(s.l.exitDetail().find("stopped") != std::string::npos);
  CHECK_EQ(s.count("rensga: frame 3"), 1);  // stdout + file copies are one line
  CHECK_EQ(s.count("boot chatter"), 0);     // emulator chatter is dropped
  CHECK_EQ(s.count("Unimplemented HLE function sceFake"), 1);  // the same emulator error every frame is shown once
  CHECK(s.count("repeated") >= 1);
  CHECK(fs::exists(d / "Library" / "Logs" / "last-play.log"));
  std::ifstream f(d / "Library" / "Logs" / "last-play.log");
  std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(all.find("rensga: boot") != std::string::npos);
  bool pinned = false;
  for (auto& e : s.events) pinned |= e.find("(pinned)") != std::string::npos;
  CHECK(pinned);
}

TEST(launcher_warns_on_other_version) {
  fs::path d = tmpDir("version");
  fakeEmulator(d, "fake_old");
  setenv("FAKE_VERSION", "v1.19.0", 1);
  Session s;
  CHECK(s.start(d, "clean", 15, "fake_old"));
  unsetenv("FAKE_VERSION");
  bool warned = false;
  for (auto& e : s.events) warned |= e.find("differs from the pinned") != std::string::npos;
  CHECK(warned);
  s.waitFor([&] { return !s.l.running(); }, 5000);
}

TEST(launcher_classifies_a_signal_as_crashed) {
  fs::path d = tmpDir("crash");
  fakeEmulator(d, "fake");
  Session s;
  CHECK(s.start(d, "crash"));
  CHECK(s.waitFor([&] { return !s.l.running(); }, 8000));
  CHECK(s.l.exitState() == pipeline::ExitState::Crashed);
  CHECK_EQ(s.l.exitSignal(), SIGSEGV);
  CHECK(s.l.exitDetail().find("SIGSEGV") != std::string::npos);
  s.drain();
  CHECK(s.count("crashed") == 1);
  CHECK(fs::exists(d / "Library" / "Logs" / "last-play.log"));
}

TEST(launcher_classifies_exit_codes) {
  fs::path d = tmpDir("exit");
  fakeEmulator(d, "fake");
  {
    Session s;
    CHECK(s.start(d, "exit3"));
    CHECK(s.waitFor([&] { return !s.l.running(); }, 8000));
    CHECK(s.l.exitState() == pipeline::ExitState::Crashed);
    CHECK_EQ(s.l.exitCode(), 3);
  }
  {
    Session s;
    CHECK(s.start(d, "clean"));
    CHECK(s.waitFor([&] { return !s.l.running(); }, 8000));
    CHECK(s.l.exitState() == pipeline::ExitState::Exited);
    CHECK_EQ(s.l.exitCode(), 0);
    CHECK(s.l.exitDetail().find("normally") != std::string::npos);
  }
}

TEST(launcher_detects_a_hang) {
  fs::path d = tmpDir("hang");
  fakeEmulator(d, "fake");
  Session s;
  CHECK(s.start(d, "hang", 2));
  CHECK(s.waitFor([&] { return s.l.exitState() == pipeline::ExitState::Hung; }, 8000));
  CHECK(s.l.running());  // alive, reported, not killed behind the user's back
  CHECK(s.l.exitDetail().find("heartbeat") != std::string::npos);
  s.drain();
  CHECK(s.count("hung:") == 1);
  s.l.stop();
  CHECK(!s.l.running());
  CHECK(fs::exists(d / "Library" / "Logs" / "last-play.log"));
}

TEST(launcher_pause_falls_back_to_sigstop) {
  fs::path d = tmpDir("pause");
  fakeEmulator(d, "fake");
  Session s;
  CHECK(s.start(d, "run"));
  CHECK(s.waitFor([&] { return s.count("rensga: frame 2") >= 1; }, 5000));
  CHECK(s.l.pause());  // the fake game ignores rensga.ctl
  CHECK(s.l.paused());
  CHECK(s.waitFor([&] { return s.count("SIGSTOP") >= 1; }, 6000));
  s.drain();
  const size_t before = s.lines.size();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  s.drain();
  CHECK_EQ(s.lines.size(), before);  // frozen: no new lines
  CHECK(s.l.exitState() == pipeline::ExitState::Running);  // frozen on purpose is not a hang
  CHECK(s.l.resume());
  CHECK(!s.l.paused());
  const int c = s.count("rensga: frame");
  CHECK(s.waitFor([&] { return s.count("rensga: frame") > c; }, 4000));
  s.l.stop();
  CHECK(!s.l.running());
}

TEST(launcher_writes_ctl_commands) {
  fs::path d = tmpDir("ctl");
  fakeEmulator(d, "fake");
  Session s;
  CHECK(s.start(d, "run"));
  CHECK(s.waitFor([&] { return s.count("rensga: frame 1") >= 1; }, 5000));
  CHECK(s.l.step(5));
  std::ifstream f(d / "build" / "rensga.ctl");
  std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(all.find("1 step 5") != std::string::npos);
  CHECK(s.l.resume());
  std::ifstream g(d / "build" / "rensga.ctl");
  std::string all2((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
  CHECK(all2.find("2 resume") != std::string::npos);
  CHECK(all2.find("1 step 5") != std::string::npos);  // earlier commands stay, so a slow poll loses none
  s.l.stop();
}
