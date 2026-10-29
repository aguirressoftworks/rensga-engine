// Debug::Log for game scripts, routed to the "game" log channel.
#pragma once
#include "rensga/core/log.h"

namespace rensga {

struct Debug {
  static void Log(const char* fmt, ...) RENSGA_PRINTF_LIKE(1, 2);
  static void LogWarning(const char* fmt, ...) RENSGA_PRINTF_LIKE(1, 2);
  static void LogError(const char* fmt, ...) RENSGA_PRINTF_LIKE(1, 2);
};

}  // namespace rensga
