#include "rensga/editor/editor_log.h"

#include <cstdio>

namespace rensga {
namespace editor {

EditorLog& EditorLog::instance() {
  static EditorLog log;
  return log;
}

void EditorLog::add(LogSource src, LogLevel lvl, std::string text) {
  std::lock_guard<std::mutex> g(mu_);
  if (echo_) std::printf("[%s] %s\n", src == LogSource::Game ? "game" : "editor", text.c_str());
  lines_.push_back({src, lvl, std::move(text)});
  if (lines_.size() > 5000) lines_.erase(lines_.begin(), lines_.begin() + 1000);
}

void EditorLog::game(const std::string& line) {
  LogLevel l = LogLevel::Info;
  if (line.rfind("ERROR", 0) == 0 || line.find("error") != std::string::npos) l = LogLevel::Error;
  else if (line.rfind("WARN", 0) == 0) l = LogLevel::Warn;
  add(LogSource::Game, l, line);
}

std::vector<LogLine> EditorLog::snapshot() const {
  std::lock_guard<std::mutex> g(mu_);
  return lines_;
}

}  // namespace editor
}  // namespace rensga
