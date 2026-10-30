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
  LogLine l;
  l.source = src;
  l.level = lvl;
  l.text = std::move(text);
  lines_.push_back(std::move(l));
  if (lines_.size() > 5000) lines_.erase(lines_.begin(), lines_.begin() + 1000);
}

void EditorLog::addDiagnostic(LogLevel lvl, std::string text, std::string file, int line, int col, int build) {
  std::lock_guard<std::mutex> g(mu_);
  if (echo_) std::printf("[editor] %s\n", text.c_str());
  LogLine l;
  l.source = LogSource::Editor;
  l.level = lvl;
  l.text = std::move(text);
  l.file = std::move(file);
  l.line = line;
  l.col = col;
  l.build = build;
  lines_.push_back(std::move(l));
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
