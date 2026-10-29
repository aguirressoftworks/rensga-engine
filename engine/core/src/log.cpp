#include "rensga/core/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace rensga {

static const char* levelName(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "?";
}

void StdoutSink::write(const LogRecord& r) {
  fprintf(stdout, "%s [%s] %s\n", levelName(r.level), r.channel, r.message);
}
void StdoutSink::flush() { fflush(stdout); }

FileSink::~FileSink() { close(); }
bool FileSink::open(const char* path) {
  close();
  file_ = fopen(path, "w");
  return file_ != nullptr;
}
void FileSink::close() {
  if (file_) {
    fclose(static_cast<FILE*>(file_));
    file_ = nullptr;
  }
}
void FileSink::write(const LogRecord& r) {
  if (!file_) return;
  fprintf(static_cast<FILE*>(file_), "%s [%s] %s\n", levelName(r.level), r.channel, r.message);
  fflush(static_cast<FILE*>(file_));
}
void FileSink::flush() {
  if (file_) fflush(static_cast<FILE*>(file_));
}

namespace log {

namespace {
struct State {
  ILogSink* sinks[kMaxSinks] = {};
  u32 count = 0;
  LogLevel min = LogLevel::Debug;
  StdoutSink defaultSink;
  bool defaultUsed = false;
};
State& state() {
  static State s;
  return s;
}
}  // namespace

bool addSink(ILogSink* sink) {
  State& s = state();
  if (!sink || s.count >= kMaxSinks) return false;
  s.sinks[s.count++] = sink;
  return true;
}
void removeSink(ILogSink* sink) {
  State& s = state();
  u32 j = 0;
  for (u32 i = 0; i < s.count; ++i)
    if (s.sinks[i] != sink) s.sinks[j++] = s.sinks[i];
  s.count = j;
}
void clearSinks() {
  State& s = state();
  s.count = 0;
  s.defaultUsed = true;  // explicit clear: do not silently re-add stdout
}
void setMinLevel(LogLevel level) { state().min = level; }
LogLevel minLevel() { return state().min; }

void ensureDefaultSink() {
  State& s = state();
  if (s.count == 0 && !s.defaultUsed) {
    s.defaultUsed = true;
    addSink(&s.defaultSink);
  }
}

void write(LogLevel level, const char* channel, const char* fmt, ...) {
  State& s = state();
  if (static_cast<int>(level) < static_cast<int>(s.min)) return;
  ensureDefaultSink();
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  LogRecord rec{level, channel, buf};
  for (u32 i = 0; i < s.count; ++i) s.sinks[i]->write(rec);
}

void flushAll() {
  State& s = state();
  for (u32 i = 0; i < s.count; ++i) s.sinks[i]->flush();
}

}  // namespace log

void assertFail(const char* expr, const char* msg, const char* file, int line) {
  log::setMinLevel(LogLevel::Debug);  // an assert is always written
  log::write(LogLevel::Error, "assert", "ASSERT failed: %s (%s) at %s:%d", msg ? msg : "", expr, file,
             line);
  log::flushAll();
  abort();
}

}  // namespace rensga
