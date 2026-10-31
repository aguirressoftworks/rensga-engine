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
  if (echo_) std::printf("[%s] %s\n", src == LogSource::Game ? "game" : src == LogSource::Emulator ? "emulator" : "editor", text.c_str());
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
  // Lines of the emulator process and the launcher are tagged by the launcher; crashes, hangs and warnings get their level from the word.
  const bool emu = line.rfind("[emu]", 0) == 0 || line.rfind("[launcher]", 0) == 0;
  if (emu) {
    const std::string text = line.rfind("[emu] ", 0) == 0 ? line.substr(6) : line;  // "[emu]" only routes the line; the Console labels it
    if (text.find("crashed") != std::string::npos || text.find("hung:") != std::string::npos || text.find(" E[") != std::string::npos) l = LogLevel::Error;
    else if (text.find("warning") != std::string::npos || text.find("did not answer") != std::string::npos) l = LogLevel::Warn;
    add(LogSource::Emulator, l, text);
    return;
  }
  if (line.rfind("ERROR", 0) == 0 || line.find("error") != std::string::npos) l = LogLevel::Error;
  else if (line.rfind("WARN", 0) == 0) l = LogLevel::Warn;
  add(LogSource::Game, l, line);
}

void EditorLog::clear() {
  std::lock_guard<std::mutex> g(mu_);
  lines_.clear();
}

size_t EditorLog::size() const {
  std::lock_guard<std::mutex> g(mu_);
  return lines_.size();
}

std::vector<LogLine> EditorLog::snapshot() const {
  std::lock_guard<std::mutex> g(mu_);
  return lines_;
}

}  // namespace editor
}  // namespace rensga
