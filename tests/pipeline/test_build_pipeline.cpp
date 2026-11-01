// F6.05 / F6.06 host checks without a toolchain or emulator: the platform table, project name / output folder rules, the process launcher
// (a shell script plays the game), Build Settings refusals of the pipeline, the profiler stream line format and the editor model that reads it.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "prof_stream.h"
#include "rensga/editor/profiler_model.h"
#include "rensga/editor/project_settings.h"
#include "rensga/pipeline/pipeline.h"
#include "rensga/pipeline/platform_build.h"
#include "rensga/pipeline/process_launcher.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

namespace {
fs::path tmp(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / "build_pipeline" / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
void writeFile(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
}
fs::path makeProject(const char* name, const char* profile, const char* scenes = "[\"00000000-0000-4000-a000-000000000001\"]") {
  fs::path d = tmp(name);
  writeFile(d / "ProjectSettings" / "Project.json", std::string("{\"format\":1,\"name\":\"My Game!\",\"target\":\"") + profile + "\",\"scenes\":" + scenes + "}");
  writeFile(d / "ProjectSettings" / "ConsoleTarget.json", std::string("{\"format\":1,\"profile\":\"") + profile + "\"}");
  return d;
}
}  // namespace

TEST(build_platform_table) {
  const auto& all = pipeline::platformInfos();
  CHECK(all.size() == 5);
  CHECK(pipeline::findPlatform("psp") && pipeline::findPlatform("psp")->console);
  CHECK(pipeline::findPlatform("ps2") && pipeline::findPlatform("ps2")->moduleId == "ps2-build");
  CHECK(pipeline::findPlatform("linux") && pipeline::findPlatform("linux")->moduleId.empty());
  CHECK(!pipeline::findPlatform("dreamcast"));
  const std::vector<std::string> psp = pipeline::platformsForProject(makeProject("p_psp", "psp"));
  CHECK(psp.size() == 4 && psp[0] == "psp" && psp[1] == "windows" && psp[2] == "linux" && psp[3] == "android");
  const std::vector<std::string> ps2 = pipeline::platformsForProject(makeProject("p_ps2", "ps2"));
  CHECK(ps2.size() == 4 && ps2[0] == "ps2" && ps2[1] == "windows");
  CHECK(pipeline::cookTargetFor("windows") == "windows" && pipeline::cookTargetFor("psp") == "psp");
}

TEST(build_names_and_folders) {
  const fs::path p = makeProject("names", "psp");
  CHECK(pipeline::projectBuildName(p) == "My_Game_");
  CHECK(pipeline::platformOutputDir(p, "linux", {}) == p / "Builds" / "linux");
  CHECK(pipeline::platformOutputDir(p, "psp", "Out") == p / "Out" / "psp");
  CHECK(pipeline::platformOutputDir(p, "psp", "/abs/out") == fs::path("/abs/out") / "psp");
}

TEST(build_settings_scene_list_roundtrip) {
  const fs::path p = makeProject("scenes", "psp", "[\"00000000-0000-4000-a000-00000000000a\",\"00000000-0000-4000-a000-00000000000b\"]");
  editor::ProjectBuildSettings s;
  std::string err;
  CHECK(editor::loadProjectBuild(p, s, err));
  CHECK(s.enabledScenes().size() == 2);
  s.addScene("00000000-0000-4000-a000-00000000000c", true);
  s.setSceneEnabled("00000000-0000-4000-a000-00000000000a", false);
  s.moveScene(2, 0);
  s.development = true;
  s.extra["buildPlatform"] = "linux";
  CHECK(editor::saveProjectBuild(p, s, err));
  editor::ProjectBuildSettings t;
  CHECK(editor::loadProjectBuild(p, t, err));
  CHECK(t.scenes.size() == 3 && t.scenes[0].guid == "00000000-0000-4000-a000-00000000000c" && !t.scenes[1].enabled);
  CHECK(t.enabledScenes().size() == 2 && t.enabledScenes()[1] == "00000000-0000-4000-a000-00000000000b");
  CHECK(t.development && t.extra.value("buildPlatform", std::string()) == "linux");
}

TEST(build_pipeline_refusals) {
  pipeline::Session session;
  std::string last;
  auto sink = [&](const pipeline::Event& e) { if (e.status == pipeline::Status::Failed) last = e.message; };
  pipeline::BuildRequest r;
  r.project = makeProject("refuse1", "psp");
  r.platform = "ps2";  // one project = one profile: a psp project cannot build a PS2 image
  CHECK(!session.run(r, sink));
  CHECK(last.find("ps2") != std::string::npos && last.find("profile") != std::string::npos);
  r.platform = "dreamcast";
  CHECK(!session.run(r, sink));
  CHECK(last.find("unknown platform") != std::string::npos);
  r.platform = "linux";
  r.project = makeProject("refuse2", "psp", "[]");  // no enabled scene
  CHECK(!session.run(r, sink));
  CHECK(last.find("no scene is enabled") != std::string::npos);
}

TEST(build_process_launcher_states) {
  const fs::path d = tmp("launcher");
  auto run = [&](const std::string& script, int hang, int boot, bool beat) {
    pipeline::ProcessLauncher::Config c;
    c.argv = {"sh", "-c", script};
    c.name = "fake player";
    c.heartbeat = beat;
    auto* l = new pipeline::ProcessLauncher(c);
    pipeline::LaunchSpec spec;
    spec.workingDir = d;
    spec.hangTimeoutSeconds = hang;
    spec.bootTimeoutSeconds = boot;
    CHECK(l->start(spec, nullptr));
    return l;
  };
  {  // a clean run: lines arrive, exit 0 = Exited
    std::unique_ptr<pipeline::ProcessLauncher> l(run("echo 'rensga: frame 60'; echo second", 5, 5, true));
    std::vector<std::string> all;
    for (int i = 0; i < 100 && l->running(); ++i) { for (auto& s : l->drainLog()) all.push_back(s); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    for (auto& s : l->drainLog()) all.push_back(s);
    CHECK(!l->running());
    CHECK(all.size() == 2 && all[0] == "rensga: frame 60");
    CHECK(l->exitState() == pipeline::ExitState::Exited && l->exitCode() == 0);
  }
  {  // exit code 3 = Crashed with the code in the sentence
    std::unique_ptr<pipeline::ProcessLauncher> l(run("echo hi; exit 3", 5, 5, true));
    for (int i = 0; i < 100 && l->running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(l->exitState() == pipeline::ExitState::Crashed && l->exitCode() == 3);
    CHECK(l->exitDetail().find("exit code 3") != std::string::npos);
  }
  {  // prints a heartbeat, then goes quiet = Hung after the timeout; stop() ends it and is reported as a stop
    std::unique_ptr<pipeline::ProcessLauncher> l(run("echo 'rensga: frame 1'; sleep 30", 1, 5, true));
    for (int i = 0; i < 150 && l->exitState() != pipeline::ExitState::Hung; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(l->exitState() == pipeline::ExitState::Hung);
    l->stop();
    CHECK(!l->running());
  }
  {  // no heartbeat expected (wine, adb): never Hung
    std::unique_ptr<pipeline::ProcessLauncher> l(run("sleep 2", 1, 1, false));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(l->exitState() == pipeline::ExitState::Running);
    l->stop();
  }
}

TEST(profiler_stream_line_roundtrip) {
  ProfFrameRecord r;
  r.frame = 42; r.totalUs = 51000; r.sectionUs[0] = 1200; r.sectionUs[1] = 800; r.sectionUs[2] = 48000; r.sectionUs[3] = 100;
  r.draws = 5; r.triangles = 1234; r.textures = 3; r.vramBytes = 65536; r.memUsed = 241856; r.memCapacity = 20643840;
  r.sampleCount = 3;
  r.samples[0] = {"AI think", 900, 1, -1};   // a space in a name must not break the line
  r.samples[1] = {"Path:find", 700, 2, 0};
  r.samples[2] = {"Move", 40, 1, -1};
  char line[512];
  const u32 n = profFormatFrame(r, line, sizeof line);
  CHECK(n > 40 && n < 400);
  editor::ProfFrameData f;
  CHECK(editor::parseProfFrameLine(std::string("[game] INFO ") + line, f));
  CHECK(f.frame == 42 && f.totalUs == 51000 && f.sectionUs[2] == 48000 && f.draws == 5 && f.triangles == 1234 && f.memCapacity == 20643840);
  CHECK(f.samples.size() == 3 && f.samples[0].name == "AI_think" && f.samples[1].name == "Path_find" && f.samples[1].parent == 0 && f.samples[1].calls == 2);
  CHECK(f.samples[2].parent == -1);
  ProfFrameRecord empty;
  empty.frame = 7; empty.totalUs = 100;
  profFormatFrame(empty, line, sizeof line);
  CHECK(std::string(line).find(" s=-") != std::string::npos);
  CHECK(editor::parseProfFrameLine(line, f) && f.frame == 7 && f.samples.empty());
  CHECK(!editor::parseProfFrameLine("rensga: prof frames=60 total_us=1/2", f));
}

TEST(profiler_feed_log_and_ring) {
  editor::ProfilerFeed& feed = editor::ProfilerFeed::instance();
  feed.clear();
  CHECK(!feed.feedLogLine("rensga: frame 60 avg=1.0ms"));
  for (unsigned i = 1; i <= 1500; ++i) {
    ProfFrameRecord r;
    r.frame = i; r.totalUs = 16000 + i;
    char line[256];
    profFormatFrame(r, line, sizeof line);
    CHECK(feed.feedLogLine(line));
  }
  const auto snap = feed.snapshot();
  CHECK(snap.size() == editor::ProfilerFeed::kMax);
  CHECK(snap.back().frame == 1500 && snap.front().frame == 1500 - editor::ProfilerFeed::kMax + 1);
  CHECK(feed.totalFrames() == 1500 && feed.source() == editor::ProfilerFeed::Source::Log);
  feed.clear();
  CHECK(feed.snapshot().empty() && feed.source() == editor::ProfilerFeed::Source::None);
}

TEST(profiler_stream_tcp_roundtrip) {
#if defined(RENSGA_PROF_SOCKET)
  setenv("RENSGA_PROF_STREAM", "tcp:47931", 1);
  ProfStream stream;
  stream.init();
  CHECK(stream.active() && stream.port() == 47931 && !stream.logging());
  editor::ProfilerFeed& feed = editor::ProfilerFeed::instance();
  feed.clear();
  CHECK(feed.attachTcp(47931, 3000));
  for (int i = 0; i < 100 && feed.state() != "connected"; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ProfFrameRecord warm;  // push() accepts the pending client
    stream.push(warm);
  }
  feed.clear();
  for (unsigned i = 1; i <= 50; ++i) {
    ProfFrameRecord r;
    r.frame = 1000 + i; r.totalUs = 5000 + i;
    r.sampleCount = 1;
    r.samples[0] = {"Job", i, 1, -1};
    stream.push(r);
  }
  for (int i = 0; i < 150; ++i) {
    const auto s = feed.snapshot();
    if (!s.empty() && s.back().frame == 1050) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(feed.totalFrames() >= 50);
  const auto snap = feed.snapshot();
  CHECK(!snap.empty() && snap.back().frame == 1050 && snap.back().samples.size() == 1 && snap.back().samples[0].us == 50);
  CHECK(feed.source() == editor::ProfilerFeed::Source::Tcp);
  feed.detach();
  stream.shutdown();
  unsetenv("RENSGA_PROF_STREAM");
#endif
}
