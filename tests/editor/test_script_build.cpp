// F3.03: compiler output parsing, the background build job (fake commands), script templates, editor command.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#include "rensga/editor/script_assets.h"
#include "rensga/editor/script_build.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;

static fs::path tmpDir(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}

static void writeFile(const fs::path& p, const std::string& text) {
  std::ofstream(p, std::ios::binary) << text;
}

TEST(script_parse_gcc) {
  auto v = parseCompilerOutput(
      "In file included from /p/Assets/Scripts/A.cpp:3:\n"
      "/p/Assets/Scripts/B.h:7:12: error: expected ';' after member declaration\n"
      "/p/Assets/Scripts/A.cpp:12:5: warning: unused variable 'x' [-Wunused-variable]\n"
      "/p/Assets/Scripts/A.cpp:14:1: note: declared here\n"
      "/p/Assets/Scripts/A.cpp:20:3: fatal error: no include path in which to search for q.h\n"
      "just a normal line: 12 files\n");
  CHECK_EQ(v.size(), size_t(5));
  CHECK(v[0].severity == DiagSeverity::Note && v[0].file == "/p/Assets/Scripts/A.cpp" && v[0].line == 3);
  CHECK(v[1].severity == DiagSeverity::Error && v[1].file == "/p/Assets/Scripts/B.h" && v[1].line == 7 && v[1].col == 12);
  CHECK(v[1].message == "expected ';' after member declaration");
  CHECK(v[2].severity == DiagSeverity::Warning && v[2].line == 12 && v[2].col == 5);
  CHECK(v[3].severity == DiagSeverity::Note);
  CHECK(v[4].severity == DiagSeverity::Error && v[4].line == 20);
}

TEST(script_parse_clang_and_linker) {
  auto v = parseCompilerOutput(
      "/p/A.cpp:9:17: error: use of undeclared identifier 'boom'\n"
      "                ^\n"
      "/usr/bin/ld: a.o: in function `f': a.cpp:(.text+0x1): undefined reference to `g()'\n"
      "collect2: error: ld returned 1 exit status\n"
      "CMake Error at CMakeLists.txt:12 (add_library):\n");
  CHECK_EQ(v.size(), size_t(4));
  CHECK(v[0].line == 9 && v[0].col == 17 && v[0].file == "/p/A.cpp");
  CHECK(v[1].file.empty() && v[1].severity == DiagSeverity::Error);
  CHECK(v[2].file.empty());
  CHECK(v[3].file == "CMakeLists.txt" && v[3].line == 12);
}

TEST(script_parse_msvc) {
  auto v = parseCompilerOutput(
      "C:\\proj\\Assets\\Scripts\\A.cpp(12,5): error C2065: 'x': undeclared identifier [C:\\b\\game.vcxproj]\n"
      "C:\\proj\\Assets\\Scripts\\A.cpp(30): warning C4101: 'y': unreferenced local variable\n"
      "LINK : fatal error LNK1104: cannot open file 'x.lib'\n");
  CHECK_EQ(v.size(), size_t(3));
  CHECK(v[0].file == "C:\\proj\\Assets\\Scripts\\A.cpp" && v[0].line == 12 && v[0].col == 5);
  CHECK(v[0].severity == DiagSeverity::Error);
  CHECK(v[0].message == "C2065: 'x': undeclared identifier");
  CHECK(v[1].severity == DiagSeverity::Warning && v[1].line == 30 && v[1].col == 0);
  CHECK(v[2].severity == DiagSeverity::Error && v[2].file.empty());
}

TEST(script_parse_progress) {
  float f = 0;
  CHECK(parseProgressLine("[ 42%] Building CXX object x.o", f));
  CHECK_NEAR(f, 0.42f, 0.001f);
  CHECK(parseProgressLine("[100%] Linking", f));
  CHECK_NEAR(f, 1.0f, 0.001f);
  CHECK(parseProgressLine("[3/12] Building CXX object", f));
  CHECK_NEAR(f, 0.25f, 0.001f);
  CHECK(!parseProgressLine("Scanning dependencies", f));
  CHECK(!parseProgressLine("[note] hi", f));
}

TEST(script_format_diagnostic_relative) {
  Diagnostic d;
  d.file = "/p/Assets/Scripts/A.cpp";
  d.line = 12;
  d.col = 5;
  d.message = "boom";
  CHECK(formatDiagnostic(d, "/p") == "Assets/Scripts/A.cpp:12:5: error: boom");
  CHECK(formatDiagnostic(d, "/q") == "/p/Assets/Scripts/A.cpp:12:5: error: boom");
}

TEST(script_parallelism_flag) {
  auto c = ScriptBuildJob::withParallelism({{"cmake", "-S", "a"}, {"cmake", "--build", "b", "--target", "t"},
                                            {"cmake", "--build", "b", "-j8"}, {"cmake", "--build", "b", "--", "-k"}}, 3);
  CHECK_EQ(c[0].size(), size_t(3));
  CHECK(c[1].back() == "3" && c[1][c[1].size() - 2] == "-j");
  CHECK_EQ(c[2].size(), size_t(4));
  CHECK(c[3][3] == "-j" && c[3][5] == "--");
}

TEST(script_job_success_progress_and_errors) {
  fs::path d = tmpDir("job_ok");
  writeFile(d / "fake.sh",
            "#!/bin/sh\n"
            "echo '[ 10%] Building a.o'\n"
            "echo '/p/A.cpp:3:4: warning: careful'\n"
            "echo '[ 60%] Building b.o'\n"
            "echo '[100%] Built target x'\n");
  writeFile(d / "second.sh", "#!/bin/sh\necho \"cwd=$(pwd) V=$RENSGA_TEST_VAR\"\n");
  ScriptBuildJob job;
  ScriptBuildJob::Options o;
  o.commands = {{"/bin/sh", (d / "fake.sh").string()}, {"/bin/sh", (d / "second.sh").string()}};
  o.cwd = d;
  o.env = {{"RENSGA_TEST_VAR", "hello"}};
  CHECK(job.start(o));
  CHECK(!job.start(o));  // one at a time
  CHECK(job.wait(20000));
  CHECK(job.state() == ScriptBuildJob::State::Succeeded);
  CHECK_NEAR(job.progress(), 1.0f, 0.001f);
  CHECK_EQ(job.warningCount(), 1);
  CHECK_EQ(job.errorCount(), 0);
  bool sawCwd = false;
  for (const std::string& l : job.lines())
    if (l.find("V=hello") != std::string::npos && l.find("job_ok") != std::string::npos) sawCwd = true;
  CHECK(sawCwd);
  CHECK(job.takeFinished());
  CHECK(!job.takeFinished());
  auto lines = job.takeLines();
  CHECK(lines.size() >= 5);
  bool diagLine = false;
  for (const BuildLine& l : lines) if (l.isDiagnostic && l.diag.line == 3 && l.diag.col == 4) diagLine = true;
  CHECK(diagLine);
}

TEST(script_job_failure_stops_at_first_failed_command) {
  fs::path d = tmpDir("job_fail");
  writeFile(d / "bad.sh", "#!/bin/sh\necho '/p/A.cpp:5:1: error: boom'\nexit 2\n");
  writeFile(d / "never.sh", "#!/bin/sh\ntouch " + (d / "ran").string() + "\n");
  ScriptBuildJob job;
  ScriptBuildJob::Options o;
  o.commands = {{"/bin/sh", (d / "bad.sh").string()}, {"/bin/sh", (d / "never.sh").string()}};
  CHECK(job.start(o));
  CHECK(job.wait(20000));
  CHECK(job.state() == ScriptBuildJob::State::Failed);
  CHECK_EQ(job.exitCode(), 2);
  CHECK(job.errorCount() >= 1);
  CHECK(!fs::exists(d / "ran"));
  CHECK(job.progress() < 1.0f);
  // a failed start is a failure too
  ScriptBuildJob job2;
  ScriptBuildJob::Options o2;
  o2.commands = {{"/definitely/not/a/program"}};
  CHECK(job2.start(o2));
  CHECK(job2.wait(20000));
  CHECK(job2.state() == ScriptBuildJob::State::Failed);
}

TEST(script_job_cancel_kills_process_group) {
  fs::path d = tmpDir("job_cancel");
  writeFile(d / "slow.sh", "#!/bin/sh\necho '[ 5%] start'\nsleep 60 &\nwait\n");
  ScriptBuildJob job;
  ScriptBuildJob::Options o;
  o.commands = {{"/bin/sh", (d / "slow.sh").string()}};
  CHECK(job.start(o));
  for (int i = 0; i < 100 && job.lines().empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!job.lines().empty());
  CHECK(job.busy());
  const auto t0 = std::chrono::steady_clock::now();
  job.cancel();
  CHECK(job.wait(10000));
  CHECK(job.state() == ScriptBuildJob::State::Cancelled);
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(8));
  CHECK(job.takeFinished());
}

TEST(script_templates) {
  const std::string b = scriptTemplate("Mover");
  CHECK(b.find("struct Mover : Behaviour") != std::string::npos);
  CHECK(b.find("void Awake() override") != std::string::npos);
  CHECK(b.find("void Start() override") != std::string::npos);
  CHECK(b.find("void Update() override") != std::string::npos);
  CHECK(b.find("RANGE(") != std::string::npos && b.find("SERIALIZE_FIELD(Scalar, speed") != std::string::npos);
  const std::string c = scriptTemplate("Health", ScriptKind::Component);
  CHECK(c.find("struct Health : Component") != std::string::npos);
  CHECK(c.find("REFLECT(Health)") != std::string::npos && c.find("Update") == std::string::npos);

  fs::path root = tmpDir("tpl_project");
  fs::create_directories(root / "Assets" / "Scripts");
  fs::path out;
  std::string err;
  CHECK(createScriptAsset(root, "Health", out, err, ScriptKind::Component));
  CHECK(createScriptAsset(root, "Mover", out, err));
  auto found = scanProjectScripts(root);
  CHECK_EQ(found.size(), size_t(2));
  CHECK(!validateScriptName(root, "Mover", {}).empty());
  CHECK(!validateScriptName(root, "9bad", {}).empty());
  CHECK(!validateScriptName(root, "class", {}).empty());
  CHECK(validateScriptName(root, "Fresh", {}).empty());
}

TEST(script_editor_command_expansion) {
  CHECK(expandEditorCommand("code -g {file}:{line}:{col}", "/a b/c.cpp", 12, 5) == "code -g '/a b/c.cpp':12:5");
  CHECK(expandEditorCommand("vim", "/x.cpp", 0, 0) == "vim '/x.cpp'");
  CHECK(expandEditorCommand("e +{line} {file}", "/it's.cpp", 3, 0) == "e +3 '/it'\\''s.cpp'");
}

TEST(script_open_in_external_editor_runs_stub) {
  fs::path d = tmpDir("open_stub");
  writeFile(d / "stub.sh", "#!/bin/sh\necho \"$@\" > " + (d / "argv.txt").string() + "\n");
  fs::permissions(d / "stub.sh", fs::perms::owner_all);
  setenv("RENSGA_EXTERNAL_EDITOR", ("/bin/sh " + (d / "stub.sh").string() + " {file}:{line}:{col}").c_str(), 1);
  CHECK(openInExternalEditor("/p/Assets/Scripts/A.cpp", 12, 5));
  bool ok = false;
  for (int i = 0; i < 100 && !ok; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::ifstream in(d / "argv.txt");
    std::string l;
    if (std::getline(in, l)) ok = l == "/p/Assets/Scripts/A.cpp:12:5";
  }
  CHECK(ok);
  unsetenv("RENSGA_EXTERNAL_EDITOR");
}
