// F3.03: building game scripts from the editor without freezing it. A ScriptBuildJob runs the module's build
// commands (configure, then build) in a child process on a worker thread, streams the output, tracks progress and
// turns compiler messages into Diagnostics. No ImGui, no console names.
#pragma once
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rensga {
namespace editor {

enum class DiagSeverity { Note, Warning, Error };

struct Diagnostic {
  std::string file;  // as the compiler printed it (empty for linker messages without a file)
  int line = 0;
  int col = 0;
  DiagSeverity severity = DiagSeverity::Error;
  std::string message;
};

// One output line -> a diagnostic. Understands GCC/Clang ("f.cpp:12:5: error: msg", "In file included from f.h:3:")
// and MSVC ("f.cpp(12,5): error C2065: msg"), plus linker "undefined reference" / "ld returned" lines.
bool parseDiagnosticLine(const std::string& line, Diagnostic& out);
// Every diagnostic in a block of compiler output.
std::vector<Diagnostic> parseCompilerOutput(const std::string& text);
// Progress of a build tool line: "[ 42%] Building ..." (make) or "[3/10] Building ..." (ninja). False if none.
bool parseProgressLine(const std::string& line, float& fraction);

// "path:line:col" with the path made relative to `root` when it lies below it.
std::string formatDiagnostic(const Diagnostic& d, const std::filesystem::path& root);

// Any build output line the job produced, with its diagnostic when it is one.
struct BuildLine {
  std::string text;
  bool isDiagnostic = false;
  Diagnostic diag;
};

class ScriptBuildJob {
 public:
  enum class State { Idle, Running, Succeeded, Failed, Cancelled };
  struct Options {
    std::vector<std::vector<std::string>> commands;  // argv lists, run in order; the first failure stops the job
    std::filesystem::path cwd;                       // working directory of the commands ("" = inherit)
    std::vector<std::pair<std::string, std::string>> env;  // extra environment variables
    int jobs = 0;  // parallelism added to "--build" commands that have no -j; 0 = $RENSGA_JOBS, else 2
    bool lowPriority = true;
  };

  ScriptBuildJob() = default;
  ~ScriptBuildJob();  // cancels a running build and joins
  ScriptBuildJob(const ScriptBuildJob&) = delete;
  ScriptBuildJob& operator=(const ScriptBuildJob&) = delete;

  bool start(Options opts);  // false if one is running
  void cancel();             // kills the child's process group; State becomes Cancelled
  State state() const { return state_.load(); }
  bool busy() const { return state_.load() == State::Running; }
  float progress() const { return progress_.load(); }  // 0..1, never decreases within a run
  int percent() const { return static_cast<int>(progress_.load() * 100.0f + 0.5f); }
  int exitCode() const { return exit_.load(); }
  // Lines produced since the last call (any thread). Safe while running.
  std::vector<BuildLine> takeLines();
  std::vector<std::string> lines() const;
  std::vector<Diagnostic> diagnostics() const;
  int errorCount() const;
  int warningCount() const;
  // Waits for the worker to end (tests). True if it ended within `ms`.
  bool wait(int ms);
  // True exactly once after a run ended: lets the UI thread react to the end. Joins the worker.
  bool takeFinished();

  // `commands` with "-j N" added to each "--build" command that lacks a parallel flag.
  static std::vector<std::vector<std::string>> withParallelism(std::vector<std::vector<std::string>> commands, int jobs);

 private:
  void run(Options opts);
  int runOne(const std::vector<std::string>& argv, const Options& opts);  // exit code, -1 cannot start, -2 cancelled
  void progressSlice(float fractionOfCommand);
  void addLine(const std::string& text);

  std::thread th_;
  std::atomic<State> state_{State::Idle};
  std::atomic<float> progress_{0};
  std::atomic<int> exit_{0};
  std::atomic<bool> cancel_{false}, finishedFlag_{false};
  std::atomic<long> pid_{0};
  float sliceLo_ = 0, sliceHi_ = 1;  // worker thread only
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::vector<std::string> lines_;
  std::vector<BuildLine> pending_;
  std::vector<Diagnostic> diags_;
};

// Result of the last script build, for gating Play (Unity: "all compiler errors have to be fixed").
bool scriptsHaveErrors();
void setScriptsHaveErrors(bool on);

// Opens a file at a line in the user's code editor. The command comes from RENSGA_EXTERNAL_EDITOR, else the
// "externalEditor" preference, else "code -g {file}:{line}:{col}". Launched detached. line/col 0 = not given.
bool openInExternalEditor(const std::filesystem::path& file, int line = 0, int col = 0);
// The command line template in effect and `expandEditorCommand(template, file, line, col)` for tests: {file} becomes
// the quoted path, {line} and {col} numbers (col and line default to 1).
std::string externalEditorCommand();
std::string expandEditorCommand(const std::string& tmpl, const std::filesystem::path& file, int line, int col);

}  // namespace editor
}  // namespace rensga
