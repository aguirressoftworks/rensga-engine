#include "rensga/editor/script_build.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <regex>

#include "rensga/editor/editor_prefs.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------------------
// Parsing

static int toInt(const std::ssub_match& m) { return m.matched ? std::atoi(m.str().c_str()) : 0; }

static DiagSeverity severityOf(const std::string& s) {
  if (s == "warning") return DiagSeverity::Warning;
  if (s == "note") return DiagSeverity::Note;
  return DiagSeverity::Error;
}

bool parseDiagnosticLine(const std::string& line, Diagnostic& out) {
  if (line.empty()) return false;
  static const std::regex gcc(R"(^(.+?):(\d+):(?:(\d+):)?\s*(fatal error|error|warning|note):\s*(.*)$)");
  static const std::regex msvc(
      R"(^(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning|note)\s*(?:([A-Za-z]+\d+))?\s*:?\s*(.*)$)");
  static const std::regex included(R"(^\s*(?:In file included from|from)\s+(.+?):(\d+)(?::(\d+))?[:,]?\s*$)");
  static const std::regex cmakeErr(R"(^CMake Error at (.+?):(\d+) \(.*\):?\s*$)");
  std::smatch m;
  out = Diagnostic();
  if (std::regex_match(line, m, gcc)) {
    out.file = m[1].str();
    out.line = toInt(m[2]);
    out.col = toInt(m[3]);
    out.severity = severityOf(m[4].str() == "fatal error" ? "error" : m[4].str());
    out.message = m[5].str();
    return true;
  }
  if (line.find('(') != std::string::npos && std::regex_match(line, m, msvc)) {
    out.file = m[1].str();
    out.line = toInt(m[2]);
    out.col = toInt(m[3]);
    out.severity = severityOf(m[4].str() == "fatal error" ? "error" : m[4].str());
    std::string msg = m[6].str();
    // MSVC appends " [C:\...\proj.vcxproj]"
    size_t br = msg.rfind(" [");
    if (br != std::string::npos && msg.back() == ']') msg.erase(br);
    out.message = m[5].matched ? m[5].str() + ": " + msg : msg;
    return true;
  }
  if (line.find("from ") != std::string::npos && std::regex_match(line, m, included)) {
    out.file = m[1].str();
    out.line = toInt(m[2]);
    out.col = toInt(m[3]);
    out.severity = DiagSeverity::Note;
    out.message = "in file included from here";
    return true;
  }
  if (line.compare(0, 11, "CMake Error") == 0) {
    if (std::regex_match(line, m, cmakeErr)) {
      out.file = m[1].str();
      out.line = toInt(m[2]);
      out.message = "CMake configure error";
    } else {
      out.message = line;
    }
    out.severity = DiagSeverity::Error;
    return true;
  }
  if (line.find("undefined reference") != std::string::npos || line.find("ld returned") != std::string::npos ||
      line.find("LNK") != std::string::npos) {
    if (line.find("undefined reference") != std::string::npos || line.find("ld returned") != std::string::npos ||
        line.find(": error LNK") != std::string::npos || line.find("fatal error LNK") != std::string::npos) {
      out.message = line;
      out.severity = DiagSeverity::Error;
      return true;
    }
  }
  return false;
}

std::vector<Diagnostic> parseCompilerOutput(const std::string& text) {
  std::vector<Diagnostic> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    while (!line.empty() && line.back() == '\r') line.pop_back();
    Diagnostic d;
    if (parseDiagnosticLine(line, d)) out.push_back(std::move(d));
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return out;
}

bool parseProgressLine(const std::string& line, float& fraction) {
  if (line.empty() || line[0] != '[') return false;
  static const std::regex pct(R"(^\[\s*(\d+)%\])");
  static const std::regex nm(R"(^\[(\d+)/(\d+)\])");
  std::smatch m;
  if (std::regex_search(line, m, pct)) {
    fraction = std::min(1.0f, static_cast<float>(std::atoi(m[1].str().c_str())) / 100.0f);
    return true;
  }
  if (std::regex_search(line, m, nm)) {
    const float n = static_cast<float>(std::atoi(m[1].str().c_str())), d = static_cast<float>(std::atoi(m[2].str().c_str()));
    if (d <= 0) return false;
    fraction = std::min(1.0f, n / d);
    return true;
  }
  return false;
}

std::string formatDiagnostic(const Diagnostic& d, const fs::path& root) {
  std::string file = d.file;
  if (!file.empty() && !root.empty()) {
    std::error_code ec;
    fs::path rel = fs::relative(fs::path(file), root, ec);
    if (!ec && !rel.empty() && *rel.begin() != "..") file = rel.generic_string();
  }
  const char* sev = d.severity == DiagSeverity::Error ? "error" : d.severity == DiagSeverity::Warning ? "warning" : "note";
  std::string s;
  if (!file.empty()) {
    s = file;
    if (d.line > 0) s += ":" + std::to_string(d.line);
    if (d.line > 0 && d.col > 0) s += ":" + std::to_string(d.col);
    s += ": ";
  }
  return s + sev + ": " + d.message;
}

// ---------------------------------------------------------------------------------------------------------------
// Job

std::vector<std::vector<std::string>> ScriptBuildJob::withParallelism(std::vector<std::vector<std::string>> commands, int jobs) {
  if (jobs <= 0) {
    const char* e = std::getenv("RENSGA_JOBS");
    jobs = e && *e ? std::atoi(e) : 2;
    if (jobs <= 0) jobs = 2;
  }
  for (auto& argv : commands) {
    if (std::find(argv.begin(), argv.end(), "--build") == argv.end()) continue;
    bool has = false;
    auto dashdash = argv.end();
    for (auto it = argv.begin(); it != argv.end(); ++it) {
      if (*it == "--") { dashdash = it; break; }
      if (it->compare(0, 2, "-j") == 0 || *it == "--parallel") has = true;
    }
    if (!has) argv.insert(dashdash, {"-j", std::to_string(jobs)});
  }
  return commands;
}

ScriptBuildJob::~ScriptBuildJob() {
  cancel();
  if (th_.joinable()) th_.join();
}

bool ScriptBuildJob::start(Options opts) {
  if (state_.load() == State::Running) return false;
  if (th_.joinable()) th_.join();
  {
    std::lock_guard<std::mutex> g(mu_);
    lines_.clear();
    pending_.clear();
    diags_.clear();
  }
  cancel_ = false;
  finishedFlag_ = false;
  progress_ = 0;
  exit_ = 0;
  state_ = State::Running;
  opts.commands = withParallelism(std::move(opts.commands), opts.jobs);
  th_ = std::thread([this, o = std::move(opts)]() mutable { run(std::move(o)); });
  return true;
}

void ScriptBuildJob::cancel() {
  if (state_.load() != State::Running) return;
  cancel_ = true;
}

bool ScriptBuildJob::wait(int ms) {
  std::unique_lock<std::mutex> g(mu_);
  return cv_.wait_for(g, std::chrono::milliseconds(ms), [this] { return state_.load() != State::Running; });
}

bool ScriptBuildJob::takeFinished() {
  if (state_.load() == State::Running || !finishedFlag_.exchange(false)) return false;
  if (th_.joinable()) th_.join();
  return true;
}

std::vector<BuildLine> ScriptBuildJob::takeLines() {
  std::lock_guard<std::mutex> g(mu_);
  std::vector<BuildLine> out;
  out.swap(pending_);
  return out;
}

std::vector<std::string> ScriptBuildJob::lines() const {
  std::lock_guard<std::mutex> g(mu_);
  return lines_;
}

std::vector<Diagnostic> ScriptBuildJob::diagnostics() const {
  std::lock_guard<std::mutex> g(mu_);
  return diags_;
}

int ScriptBuildJob::errorCount() const {
  std::lock_guard<std::mutex> g(mu_);
  return static_cast<int>(std::count_if(diags_.begin(), diags_.end(), [](const Diagnostic& d) { return d.severity == DiagSeverity::Error; }));
}

int ScriptBuildJob::warningCount() const {
  std::lock_guard<std::mutex> g(mu_);
  return static_cast<int>(std::count_if(diags_.begin(), diags_.end(), [](const Diagnostic& d) { return d.severity == DiagSeverity::Warning; }));
}

void ScriptBuildJob::addLine(const std::string& text) {
  if (text.empty()) return;
  BuildLine bl;
  bl.text = text;
  bl.isDiagnostic = parseDiagnosticLine(text, bl.diag);
  float f = 0;
  if (parseProgressLine(text, f)) progressSlice(f);
  std::lock_guard<std::mutex> g(mu_);
  lines_.push_back(text);
  if (bl.isDiagnostic) diags_.push_back(bl.diag);
  pending_.push_back(std::move(bl));
}

void ScriptBuildJob::run(Options opts) {
#if defined(__linux__)
  if (opts.lowPriority) {  // threads have their own nice value on Linux: the child inherits this thread's
    const long tid = syscall(SYS_gettid);
    setpriority(PRIO_PROCESS, static_cast<id_t>(tid), 19);
    syscall(SYS_ioprio_set, 1 /*IOPRIO_WHO_PROCESS*/, tid, (2 << 13) | 7);  // best effort, lowest
  }
#endif
  State result = State::Succeeded;
  const size_t n = opts.commands.size();
  for (size_t i = 0; i < n; ++i) {
    sliceLo_ = static_cast<float>(i) / static_cast<float>(n);
    sliceHi_ = static_cast<float>(i + 1) / static_cast<float>(n);
    const int rc = runOne(opts.commands[i], opts);
    if (rc == -2) { result = State::Cancelled; break; }
    if (rc != 0) {
      exit_ = rc;
      std::string cmd;
      for (const std::string& a : opts.commands[i]) cmd += (cmd.empty() ? "" : " ") + a;
      addLine(rc < 0 ? "error: cannot start: " + cmd : "error: command failed (exit " + std::to_string(rc) + "): " + cmd);
      result = State::Failed;
      break;
    }
    progressSlice(1.0f);
  }
  if (result == State::Succeeded) progress_ = 1.0f;
  {
    std::lock_guard<std::mutex> g(mu_);
    finishedFlag_ = true;
    state_ = result;
  }
  cv_.notify_all();
}

void ScriptBuildJob::progressSlice(float f) {
  const float v = sliceLo_ + (sliceHi_ - sliceLo_) * std::min(1.0f, f);
  float cur = progress_.load();
  while (v > cur && !progress_.compare_exchange_weak(cur, v)) {}
}

#ifdef _WIN32
static std::string quoteWin(const std::string& s) {
  if (!s.empty() && s.find_first_of(" \t\"") == std::string::npos) return s;
  std::string o = "\"";
  for (char c : s) { if (c == '"') o += '\\'; o += c; }
  return o + "\"";
}

int ScriptBuildJob::runOne(const std::vector<std::string>& argv, const Options& opts) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = si.hStdError = wr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  std::string cmd;
  for (const std::string& a : argv) cmd += (cmd.empty() ? "" : " ") + quoteWin(a);
  for (const auto& kv : opts.env) SetEnvironmentVariableA(kv.first.c_str(), kv.second.c_str());
  PROCESS_INFORMATION pi{};
  DWORD flags = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | (opts.lowPriority ? BELOW_NORMAL_PRIORITY_CLASS : 0);
  const std::string cwd = opts.cwd.string();
  BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, flags, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
  CloseHandle(wr);
  if (!ok) { CloseHandle(rd); return -1; }
  pid_ = static_cast<long>(pi.dwProcessId);
  std::string acc;
  char buf[4096];
  bool cancelled = false;
  for (;;) {
    DWORD avail = 0;
    if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;  // pipe closed: the child ended
    if (cancel_.load() && !cancelled) {
      cancelled = true;
      std::string kill = "taskkill /F /T /PID " + std::to_string(pi.dwProcessId);
      std::system(kill.c_str());
    }
    if (avail == 0) { if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) { PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr); if (avail == 0) break; } continue; }
    DWORD got = 0;
    if (!ReadFile(rd, buf, sizeof buf, &got, nullptr) || got == 0) break;
    acc.append(buf, got);
    size_t nl;
    while ((nl = acc.find_first_of("\r\n")) != std::string::npos) { addLine(acc.substr(0, nl)); acc.erase(0, nl + 1); }
  }
  addLine(acc);
  DWORD code = 1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(rd);
  pid_ = 0;
  return cancelled ? -2 : static_cast<int>(code);
}
#else
int ScriptBuildJob::runOne(const std::vector<std::string>& argvIn, const Options& opts) {
  if (argvIn.empty()) return -1;
  // A working directory goes through the shell so the child needs no chdir file action: sh -c 'cd "$0" && exec "$@"'
  std::vector<std::string> argv;
  if (!opts.cwd.empty()) {
    argv = {"/bin/sh", "-c", "cd \"$0\" && exec \"$@\"", opts.cwd.string()};
    argv.insert(argv.end(), argvIn.begin(), argvIn.end());
  } else {
    argv = argvIn;
  }
  std::vector<char*> av;
  for (std::string& s : argv) av.push_back(s.data());
  av.push_back(nullptr);
  std::vector<std::string> envStore;
  for (char** e = environ; e && *e; ++e) {
    bool overridden = false;
    for (const auto& kv : opts.env)
      if (std::strncmp(*e, kv.first.c_str(), kv.first.size()) == 0 && (*e)[kv.first.size()] == '=') overridden = true;
    if (!overridden) envStore.push_back(*e);
  }
  for (const auto& kv : opts.env) envStore.push_back(kv.first + "=" + kv.second);
  std::vector<char*> envp;
  for (std::string& s : envStore) envp.push_back(s.data());
  envp.push_back(nullptr);

  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) return -1;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
  posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
  posix_spawnattr_t at;
  posix_spawnattr_init(&at);
  posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&at, 0);
  pid_t pid = 0;
  const int err = posix_spawnp(&pid, av[0], &fa, &at, av.data(), envp.data());
  posix_spawn_file_actions_destroy(&fa);
  posix_spawnattr_destroy(&at);
  close(fds[1]);
  if (err != 0) { close(fds[0]); return -1; }
  pid_ = pid;

  std::string acc;
  char buf[4096];
  bool cancelled = false;
  auto killedAt = std::chrono::steady_clock::now();
  for (;;) {
    if (cancel_.load() && !cancelled) {
      cancelled = true;
      killedAt = std::chrono::steady_clock::now();
      kill(-pid, SIGTERM);
    }
    if (cancelled && std::chrono::steady_clock::now() - killedAt > std::chrono::milliseconds(1500)) {
      kill(-pid, SIGKILL);
      break;
    }
    pollfd p{fds[0], POLLIN, 0};
    const int pr = poll(&p, 1, 100);
    if (pr < 0 && errno != EINTR) break;
    if (pr <= 0) continue;
    const ssize_t got = read(fds[0], buf, sizeof buf);
    if (got <= 0) break;  // EOF: every holder of the pipe is gone
    acc.append(buf, static_cast<size_t>(got));
    size_t nl;
    while ((nl = acc.find_first_of("\r\n")) != std::string::npos) {
      addLine(acc.substr(0, nl));
      acc.erase(0, nl + 1);
    }
  }
  addLine(acc);
  close(fds[0]);
  int st = 0;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
  pid_ = 0;
  if (cancelled) return -2;
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  return 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// Script error state, external editor

static std::atomic<bool> g_scriptErrors{false};
bool scriptsHaveErrors() { return g_scriptErrors.load(); }
void setScriptsHaveErrors(bool on) { g_scriptErrors = on; }

std::string externalEditorCommand() {
  const char* e = std::getenv("RENSGA_EXTERNAL_EDITOR");
  if (e && *e) return e;
  const std::string p = externalEditorPref();
  return p.empty() ? kDefaultExternalEditor : p;
}

std::string expandEditorCommand(const std::string& tmpl, const fs::path& file, int line, int col) {
  auto quote = [](const std::string& s) {
    std::string o = "'";
    for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return o + "'";
  };
  auto repl = [](std::string s, const std::string& key, const std::string& val) {
    for (size_t p = 0; (p = s.find(key, p)) != std::string::npos; p += val.size()) s.replace(p, key.size(), val);
    return s;
  };
  std::string t = tmpl;
  if (t.find("{file}") == std::string::npos) t += " {file}";  // a bare command (EDITOR style) gets the path
  t = repl(t, "{line}", std::to_string(line > 0 ? line : 1));
  t = repl(t, "{col}", std::to_string(col > 0 ? col : 1));
  return repl(t, "{file}", quote(file.string()));
}

#ifndef _WIN32
static bool onPath(const std::string& exe) {
  if (exe.empty()) return false;
  if (exe.find('/') != std::string::npos) return access(exe.c_str(), X_OK) == 0;
  const char* path = std::getenv("PATH");
  std::string p = path ? path : "";
  size_t pos = 0;
  while (pos <= p.size()) {
    size_t c = p.find(':', pos);
    std::string dir = p.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
    if (!dir.empty() && access((dir + "/" + exe).c_str(), X_OK) == 0) return true;
    if (c == std::string::npos) break;
    pos = c + 1;
  }
  return false;
}
#endif

bool openInExternalEditor(const fs::path& file, int line, int col) {
  std::string tmpl = externalEditorCommand();
#ifdef _WIN32
  std::string cmd = "start \"\" " + expandEditorCommand(tmpl, file, line, col);
  return std::system(cmd.c_str()) == 0;
#else
  size_t sp = tmpl.find(' ');
  const std::string exe = tmpl.substr(0, sp);
  if (!onPath(exe)) tmpl = "xdg-open";  // no "code" installed: let the desktop pick
  const std::string cmd = expandEditorCommand(tmpl, file, line, col) + " </dev/null >/dev/null 2>&1 &";
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    setsid();
    execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int st = 0;
  waitpid(pid, &st, 0);  // the shell backgrounds the editor and exits at once
  return WIFEXITED(st) && WEXITSTATUS(st) == 0;
#endif
}

}  // namespace editor
}  // namespace rensga
