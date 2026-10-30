// MVP-07: channels, levels and sinks. No heap allocation while logging.
#pragma once
#include "rensga/core/types.h"

namespace rensga {

enum class LogLevel : u8 { Debug, Info, Warn, Error };

struct LogRecord {
  LogLevel level;
  const char* channel;  // e.g. "core", "game", "gu"
  const char* message;  // NUL-terminated, no trailing newline
};

// One sink later feeds the editor Console (see CallbackSink).
class ILogSink {
 public:
  virtual ~ILogSink() {}
  virtual void write(const LogRecord& record) = 0;
  virtual void flush() {}
};

// "<LEVEL> [channel] message\n" to stdout.
class StdoutSink : public ILogSink {
 public:
  void write(const LogRecord& record) override;
  void flush() override;
};

// Same format to a file (fopen). Flushes every line so an assert/crash keeps the text.
class FileSink : public ILogSink {
 public:
  FileSink() {}
  ~FileSink() override;
  bool open(const char* path);
  void close();
  void write(const LogRecord& record) override;
  void flush() override;

 private:
  void* file_ = nullptr;
};

// Forwards records to a function (editor Console, tests).
class CallbackSink : public ILogSink {
 public:
  using Fn = void (*)(const LogRecord&, void* user);
  CallbackSink(Fn fn, void* user) : fn_(fn), user_(user) {}
  void write(const LogRecord& record) override { fn_(record, user_); }

 private:
  Fn fn_;
  void* user_;
};

class IFileSystem;

// Keeps the last kCapacity records in a fixed ring (no heap) for an in-game console or crash dump.
class RingSink : public ILogSink {
 public:
  static constexpr u32 kCapacity = 64;
  static constexpr u32 kMessageLength = 160;
  struct Entry {
    LogLevel level = LogLevel::Info;
    char channel[16] = {};
    char message[kMessageLength] = {};
  };
  void write(const LogRecord& record) override;
  u32 count() const { return count_ < kCapacity ? count_ : kCapacity; }
  u64 totalWritten() const { return count_; }
  // 0 = oldest kept record. nullptr if out of range.
  const Entry* entry(u32 index) const;
  void clear() { count_ = 0; }

 private:
  Entry entries_[kCapacity];
  u64 count_ = 0;
};

// Sends records through IFileSystem::writeLog (host0:/rensga.log on PSP). Each line starts with
// "rensga: "; a message that already starts with it is not prefixed twice.
class FsLogSink : public ILogSink {
 public:
  explicit FsLogSink(IFileSystem* fs) : fs_(fs) {}
  void write(const LogRecord& record) override;

 private:
  IFileSystem* fs_;
};

namespace log {
constexpr u32 kMaxSinks = 8;
bool addSink(ILogSink* sink);  // not owned; false if full
void removeSink(ILogSink* sink);
void clearSinks();
void setMinLevel(LogLevel level);
LogLevel minLevel();
// Installs a StdoutSink if no sink is registered yet. Called lazily by write().
void ensureDefaultSink();
void write(LogLevel level, const char* channel, const char* fmt, ...) RENSGA_PRINTF_LIKE(3, 4);
void flushAll();
}  // namespace log

// Prints the message to every sink, flushes, then aborts.
[[noreturn]] void assertFail(const char* expr, const char* msg, const char* file, int line);

}  // namespace rensga

#define RLOG_DEBUG(ch, ...) ::rensga::log::write(::rensga::LogLevel::Debug, ch, __VA_ARGS__)
#define RLOG_INFO(ch, ...) ::rensga::log::write(::rensga::LogLevel::Info, ch, __VA_ARGS__)
#define RLOG_WARN(ch, ...) ::rensga::log::write(::rensga::LogLevel::Warn, ch, __VA_ARGS__)
#define RLOG_ERROR(ch, ...) ::rensga::log::write(::rensga::LogLevel::Error, ch, __VA_ARGS__)

#define RENSGA_ASSERT(cond, msg) \
  ((cond) ? (void)0 : ::rensga::assertFail(#cond, msg, __FILE__, __LINE__))
