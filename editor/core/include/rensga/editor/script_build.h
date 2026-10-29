// MVP-57: background rebuild of the editor, CLI, cooker and headless runtime so a changed project script becomes
// part of them. Streams the compiler output into EditorLog (errors as file:line, Error level). No ImGui.
#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace rensga {
namespace editor {

struct ScriptBuildConfig {
  std::filesystem::path buildDir;  // the CMake build tree this editor was built from
  std::filesystem::path project;   // project whose scripts must be compiled in
  std::string cmake = "cmake";
  std::vector<std::string> targets;
};

// One compiler output line -> a log line. Returns false for lines that are not diagnostics.
// "<project>/Assets/Scripts/A.cpp:12:5: error: boom" -> "Assets/Scripts/A.cpp:12: boom" (level Error).
enum class DiagLevel { None, Warning, Error };
DiagLevel parseCompilerLine(const std::string& line, const std::filesystem::path& project, std::string& text);

// Value of a CMakeCache.txt entry, "" if absent.
std::string cmakeCacheValue(const std::filesystem::path& buildDir, const std::string& key);

class ScriptBuilder {
 public:
  enum class Result { None, Ok, Failed };
  ~ScriptBuilder();
  bool start(const ScriptBuildConfig& cfg);  // false if one is running
  bool busy() const { return busy_.load(); }
  int percent() const { return percent_.load(); }
  // UI thread, once per frame: Ok / Failed exactly once when a build has just ended.
  Result poll();

 private:
  void run(ScriptBuildConfig cfg);
  bool exec(const std::string& cmd, const std::filesystem::path& project);
  std::thread th_;
  std::atomic<bool> busy_{false}, finished_{false}, ok_{false};
  std::atomic<int> percent_{0};
};

}  // namespace editor
}  // namespace rensga
