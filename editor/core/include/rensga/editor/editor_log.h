// Editor console model: editor log + game log lines with levels. No ImGui.
#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace rensga {
namespace editor {

enum class LogSource { Editor, Game, Emulator };  // Emulator: lines from the emulator process and the launcher (game() routes lines tagged "[emu]" or "[launcher]")
enum class LogLevel { Info, Warn, Error };

struct LogLine {
  LogSource source = LogSource::Editor;
  LogLevel level = LogLevel::Info;
  std::string text;
  // Compiler diagnostics (script builds): where it points, and which build produced it. file is empty otherwise.
  std::string file;
  int line = 0;
  int col = 0;
  int build = 0;
};

class EditorLog {
 public:
  static EditorLog& instance();
  void add(LogSource src, LogLevel lvl, std::string text);
  void info(const std::string& t) { add(LogSource::Editor, LogLevel::Info, t); }
  void warn(const std::string& t) { add(LogSource::Editor, LogLevel::Warn, t); }
  void error(const std::string& t) { add(LogSource::Editor, LogLevel::Error, t); }
  // A compiler diagnostic: text is what the console shows, file:line:col is where a double-click goes.
  void addDiagnostic(LogLevel lvl, std::string text, std::string file, int line, int col, int build);
  void game(const std::string& line);  // level guessed from the text
  std::vector<LogLine> snapshot() const;
  void clear();                        // the console's Clear button
  size_t size() const;
  void setEcho(bool on) { echo_ = on; }  // mirror lines to stdout (headless runs, tests)

 private:
  mutable std::mutex mu_;
  std::vector<LogLine> lines_;
  bool echo_ = true;
};

}  // namespace editor
}  // namespace rensga
