#include "rensga/core/debug.h"

#include <stdarg.h>
#include <stdio.h>

namespace rensga {

static void emit(LogLevel level, const char* fmt, va_list ap) {
  char buf[400];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  log::write(level, "game", "%s", buf);
}
void Debug::Log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(LogLevel::Info, fmt, ap);
  va_end(ap);
}
void Debug::LogWarning(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(LogLevel::Warn, fmt, ap);
  va_end(ap);
}
void Debug::LogError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(LogLevel::Error, fmt, ap);
  va_end(ap);
}

}  // namespace rensga
