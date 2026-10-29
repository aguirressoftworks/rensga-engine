// MVP-28: log sinks for PSP: stdout + host0:/rensga.log. FileSink flushes every line, so an
// assert (which prints through every sink before aborting) always leaves its message behind.
#include "rensga/platform/psp.h"

namespace rensga {

void pspInstallLogSinks(const char* logPath) {
  static StdoutSink out;
  static FileSink file;
  log::clearSinks();
  log::addSink(&out);
  if (file.open(logPath)) log::addSink(&file);
}

}  // namespace rensga
