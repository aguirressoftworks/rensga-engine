// Editor console model: editor log + game log lines with levels. No ImGui.
#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace rensga {
namespace editor {

enum class LogSource { Editor, Game };
enum class LogLevel { Info, Warn, Error };

struct LogLine {
  LogSource source = LogSource::Editor;
  LogLevel level = LogLevel::Info;
  std::string text;
};

class EditorLog {
 public:
  static EditorLog& instance();
  void add(LogSource src, LogLevel lvl, std::string text);
  void info(const std::string& t) { add(LogSource::Editor, LogLevel::Info, t); }
  void warn(const std::string& t) { add(LogSource::Editor, LogLevel::Warn, t); }
  void error(const std::string& t) { add(LogSource::Editor, LogLevel::Error, t); }
  void game(const std::string& line);  // level guessed from the text
  std::vector<LogLine> snapshot() const;
  void setEcho(bool on) { echo_ = on; }  // mirror lines to stdout (headless runs, tests)

 private:
  mutable std::mutex mu_;
  std::vector<LogLine> lines_;
  bool echo_ = true;
};

}  // namespace editor
}  // namespace rensga
