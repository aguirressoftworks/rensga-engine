// MVP-37: emulator launching as a separate process (emulators are GPL: never linked).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "rensga/pipeline/pipeline.h"

namespace rensga {
namespace pipeline {

struct LaunchSpec {
  std::filesystem::path executable;  // game ELF or EBOOT to boot directly
  std::filesystem::path workingDir;  // dev build dir (host0: root)
  std::filesystem::path configDir;   // <project>/Library/Emulators/<emulator>
  int quitAfterSeconds = 0;
};

class EmulatorLauncher {
 public:
  virtual ~EmulatorLauncher() = default;
  virtual bool start(const LaunchSpec& spec, EventSink sink) = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;
  virtual std::vector<std::string> drainLog() = 0;
};

}  // namespace pipeline
}  // namespace rensga
